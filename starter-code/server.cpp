#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
namespace fs=std::filesystem; constexpr int BACKLOG=32;
struct Config { int port{}; int peer_interval{}; std::string peer_subnet; fs::path root; };
struct PeerSnapshot { std::string address; std::vector<std::string> files; std::map<std::string,std::string> entries; };
struct PeerInventory { std::vector<std::string> files; std::map<std::string,std::string> entries; };
std::mutex peers_mutex;
std::map<std::string,PeerSnapshot> peer_snapshots;
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,b-a+1);}
std::optional<Config> load_config(const std::string& name){
 std::ifstream in(name); if(!in){std::cerr<<"Unable to open configuration file: "<<name<<'\n';return{};}
 Config c; std::string line,port,interval;
 while(std::getline(in,line)){line=trim(line);if(line.empty()||line[0]=='#')continue;auto e=line.find('=');if(e==std::string::npos){std::cerr<<"Invalid configuration line\n";return{};}auto k=trim(line.substr(0,e)),v=trim(line.substr(e+1));if(k=="PORT")port=v;else if(k=="PEER_INTERVAL")interval=v;else if(k=="PEER_SUBNET")c.peer_subnet=v;else std::cerr<<"Warning: ignoring unknown key "<<k<<'\n';}
 try{if(port.empty()||interval.empty())throw std::invalid_argument("missing value");c.port=std::stoi(port);c.peer_interval=std::stoi(interval);}catch(const std::exception&e){std::cerr<<"Invalid configuration: "<<e.what()<<'\n';return{};}
 if(c.port<1||c.port>65335||c.peer_interval<0){std::cerr<<"Invalid PORT or PEER_INTERVAL\n";return{};} c.root=fs::weakly_canonical(fs::current_path()/"db");if(!fs::is_directory(c.root)){std::cerr<<"Database root does not exist: "<<c.root<<'\n';return{};}return c;
}
std::string upper(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::toupper(c);});return s;}
std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::tolower(c);});return s;}
bool send_all(int fd,std::string_view data){size_t n=0;while(n<data.size()){auto r=send(fd,data.data()+n,data.size()-n,0);if(r<0){if(errno==EINTR)continue;return false;}if(!r)return false;n+=(size_t)r;}return true;}
bool line(int fd,std::string& out){out.clear();char c;while(true){auto r=recv(fd,&c,1,0);if(r==0)return false;if(r<0){if(errno==EINTR)continue;return false;}if(c=='\n')break;if(c!='\r')out+=c;if(out.size()>4096)return false;}return true;}
bool inside_root(const fs::path& root,const fs::path& path){auto relative=path.lexically_relative(root);if(relative.empty()||relative==".")return true;if(relative.is_absolute())return false;for(const auto& component:relative){if(component=="..")return false;}return true;}
std::optional<fs::path> resolve_existing(const Config& c,const fs::path& current,const std::string& arg){if(arg.find('\0')!=std::string::npos)return{};fs::path requested(arg);auto candidate=arg.empty()?current:(requested.is_absolute()?c.root/requested.relative_path():current/requested);std::error_code ec;auto normalized=fs::canonical(candidate,ec);if(ec||!inside_root(c.root,normalized))return{};return normalized;}
std::optional<fs::path> directory(const Config& c,const fs::path& current,const std::string& arg){auto resolved=resolve_existing(c,current,arg);if(!resolved)return{};std::error_code ec;if(!fs::is_directory(*resolved,ec)||ec)return{};return resolved;}
std::string vpath(const Config& c,const fs::path& p){auto r=fs::relative(p,c.root);return r=="."?"/":"/"+r.generic_string();}
std::optional<fs::path> path_inside(const Config& c,const fs::path& current,const std::string& arg){return resolve_existing(c,current,arg);}
struct Passive{int fd{-1};};
void close_passive(Passive& passive){if(passive.fd>=0){close(passive.fd);passive.fd=-1;}}
std::optional<Passive> open_passive(){int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return{};int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);address.sin_port=0;if(bind(fd,(sockaddr*)&address,sizeof address)<0||listen(fd,1)<0){close(fd);return{};}return Passive{fd};}
std::optional<int> accept_data(Passive& passive){if(passive.fd<0)return{};fd_set readable;FD_ZERO(&readable);FD_SET(passive.fd,&readable);timeval timeout{5,0};int ready;do{ready=select(passive.fd+1,&readable,nullptr,nullptr,&timeout);}while(ready<0&&errno==EINTR);if(ready<=0){close_passive(passive);return{};}int data=accept(passive.fd,nullptr,nullptr);close_passive(passive);if(data<0)return{};return data;}
std::string passive_reply(int control_fd,const Passive& passive){sockaddr_in local{};socklen_t length=sizeof local;if(getsockname(control_fd,(sockaddr*)&local,&length)<0||local.sin_family!=AF_INET)local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(local.sin_addr.s_addr==htonl(INADDR_ANY))local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);sockaddr_in data{};length=sizeof data;if(getsockname(passive.fd,(sockaddr*)&data,&length)<0)return{};auto ip=ntohl(local.sin_addr.s_addr);auto port=ntohs(data.sin_port);std::ostringstream out;out<<"227 Entering Passive Mode ("<<((ip>>24)&255)<<","<<((ip>>16)&255)<<","<<((ip>>8)&255)<<","<<(ip&255)<<","<<(port/256)<<","<<(port%256)<<")\r\n";return out.str();}
std::string time_text(const fs::path& path){struct stat info{};if(stat(path.c_str(),&info)!=0)return "Jan 01 00:00";std::tm tm{};localtime_r(&info.st_mtime,&tm);char buffer[32];std::strftime(buffer,sizeof buffer,"%b %d %H:%M",&tm);return buffer;}
std::string list_entry(const fs::directory_entry& entry){std::error_code ec;auto is_dir=entry.is_directory(ec);auto size=is_dir?0:static_cast<long long>(entry.file_size(ec));std::ostringstream out;out<<(is_dir?"drwxr-xr-x":"-rw-r--r--")<<" 1 local "<<size<<" "<<time_text(entry.path())<<" "<<entry.path().filename().string()<<"\r\n";return out.str();}
std::string peer_listing_line(std::string entry);
std::string list_directory(const fs::path& directory){std::vector<fs::directory_entry> entries;std::error_code ec;for(fs::directory_iterator it(directory,ec);!ec&&it!=fs::directory_iterator();it.increment(ec))entries.push_back(*it);std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.path().filename().string()<b.path().filename().string();});std::map<std::string,std::string> output;for(const auto& entry:entries)output[entry.path().filename().string()]=list_entry(entry);auto root=fs::weakly_canonical(fs::current_path()/"db");auto relative=fs::relative(directory,root).generic_string();if(relative==".")relative="";std::lock_guard lock(peers_mutex);for(const auto& [address,snapshot]:peer_snapshots){(void)address;for(const auto& [path,line]:snapshot.entries){auto parent=fs::path(path).parent_path().generic_string();if(parent==".")parent="";if(parent!=relative)continue;auto name=fs::path(path).filename().string();if(!output.contains(name))output[name]=peer_listing_line(line);}}std::string result;for(const auto& [name,line]:output){(void)name;result+=line;}return result;}
std::optional<std::string> listing_name(const std::string& entry){std::istringstream fields(entry);std::string mode,links,owner,size,month,day,clock,name;if(!(fields>>mode>>links>>owner>>size>>month>>day>>clock>>name)||name.empty())return{};return name;}
std::string peer_listing_line(std::string entry){auto marker=entry.find(" local ");if(marker!=std::string::npos)entry.replace(marker,7," peer ");return entry;}
std::string unified_listing(const Config& c,const fs::path& target){std::map<std::string,std::string> entries;for(const auto& line:std::vector<std::string>{list_directory(target)}){std::istringstream lines(line);std::string current;while(std::getline(lines,current)){if(auto name=listing_name(current))entries[*name]=current+"\r\n";}}auto relative=fs::relative(target,c.root).generic_string();if(relative==".")relative="";std::lock_guard lock(peers_mutex);for(const auto& [address,snapshot]:peer_snapshots){(void)address;for(const auto& [path,line]:snapshot.entries){auto parent=fs::path(path).parent_path().generic_string();if(parent==".")parent="";if(parent!=relative)continue;if(auto name=listing_name(line);name&& !entries.contains(*name))entries[*name]=peer_listing_line(line);}}std::string result;for(const auto& [name,line]:entries){(void)name;result+=line;}return result;}
int connect_peer(const std::string& address,int port){int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return-1;sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(static_cast<uint16_t>(port));if(inet_pton(AF_INET,address.c_str(),&target.sin_addr)!=1){close(fd);return-1;}int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return-1;}int result=connect(fd,(sockaddr*)&target,sizeof target);if(result<0&&errno!=EINPROGRESS){close(fd);return-1;}if(result<0){fd_set writable;FD_ZERO(&writable);FD_SET(fd,&writable);timeval timeout{1,0};do{result=select(fd+1,nullptr,&writable,nullptr,&timeout);}while(result<0&&errno==EINTR);if(result<=0){close(fd);return-1;}int error=0;socklen_t length=sizeof error;if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)<0||error!=0){close(fd);return-1;}}if(fcntl(fd,F_SETFL,flags)<0){close(fd);return-1;}return fd;}
std::optional<std::array<int,6>> parse_pasv(const std::string& response){auto begin=response.find('('),end=response.find(')',begin);if(begin==std::string::npos||end==std::string::npos)return{};std::string values=response.substr(begin+1,end-begin-1);std::replace(values.begin(),values.end(),',',' ');std::istringstream input(values);std::array<int,6> result{};for(auto& value:result)if(!(input>>value)||value<0||value>255)return{};return result;}
std::optional<int> peer_data_socket(int control){std::string response;if(!line(control,response)||response.rfind("227 ",0)!=0)return{};auto values=parse_pasv(response);if(!values)return{};std::string address=std::to_string((*values)[0])+"."+std::to_string((*values)[1])+"."+std::to_string((*values)[2])+"."+std::to_string((*values)[3]);int port=(*values)[4]*256+(*values)[5];int data=connect_peer(address,port);if(data<0)return{};return data;}
std::optional<std::string> peer_list(int control,const std::string& path){if(!send_all(control,"PASV\r\n"))return{};auto data=peer_data_socket(control);if(!data)return{};std::string command="LIST"+(path.empty()?std::string():" "+path)+"\r\n";if(!send_all(control,command)){close(*data);return{};}std::string response;if(!line(control,response)||response.rfind("150 ",0)!=0){close(*data);return{};}std::string listing;char buffer[16384];while(true){auto count=recv(*data,buffer,sizeof buffer,0);if(count==0)break;if(count<0){if(errno==EINTR)continue;close(*data);return{};}listing.append(buffer,static_cast<size_t>(count));}close(*data);if(!line(control,response)||response.rfind("226 ",0)!=0)return{};return listing;}
std::optional<PeerInventory> peer_inventory(int control){PeerInventory inventory;std::vector<std::string> pending{""};std::set<std::string> visited;while(!pending.empty()&&visited.size()<4096){auto path=pending.back();pending.pop_back();if(!visited.insert(path).second)continue;auto listing=peer_list(control,path);if(!listing)return{};std::istringstream lines(*listing);std::string entry;while(std::getline(lines,entry)){entry=trim(entry);if(entry.empty())continue;std::istringstream fields(entry);std::string mode,links,owner,size,month,day,clock,name;if(!(fields>>mode>>links>>owner>>size>>month>>day>>clock>>name)||name.empty())continue;auto child=path.empty()?name:path+"/"+name;entry+="\r\n";inventory.entries[child]=entry;if(mode[0]=='d')pending.push_back(child);else if(mode[0]=='-')inventory.files.push_back(child);}}return inventory;}
int authenticated_peer(const std::string& address,int peer_port){int control=connect_peer(address,peer_port);if(control<0)return-1;sockaddr_in local{};socklen_t length=sizeof local;char local_address[INET_ADDRSTRLEN]{};if(getsockname(control,(sockaddr*)&local,&length)<0||!inet_ntop(AF_INET,&local.sin_addr,local_address,sizeof local_address)){close(control);return-1;}std::string response;if(!line(control,response)||response.rfind("220 ",0)!=0){close(control);return-1;}std::string user="USER peer@"+std::string(local_address)+"\r\n";if(!send_all(control,user)||!line(control,response)||response.rfind("230 ",0)!=0){close(control);return-1;}return control;}
std::optional<std::string> requested_relative(const Config& c,const fs::path& current,const std::string& arg){if(arg.empty()||arg.find('\0')!=std::string::npos)return{};fs::path requested(arg);auto base=fs::relative(current,c.root);auto combined=requested.is_absolute()?requested.relative_path():base/requested;auto normalized=combined.lexically_normal();if(normalized.is_absolute())return{};for(const auto& component:normalized)if(component=="..")return{};auto result=normalized.generic_string();if(result==".")return{};return result;}
std::optional<std::string> peer_for_file(const std::string& relative){std::lock_guard lock(peers_mutex);for(const auto& [address,snapshot]:peer_snapshots)if(std::find(snapshot.files.begin(),snapshot.files.end(),relative)!=snapshot.files.end())return address;return{};}
std::optional<PeerSnapshot> discover_peer(const std::string& address,int peer_port){int control=connect_peer(address,peer_port);if(control<0)return{};sockaddr_in local{};socklen_t length=sizeof local;char local_address[INET_ADDRSTRLEN]{};if(getsockname(control,(sockaddr*)&local,&length)<0||!inet_ntop(AF_INET,&local.sin_addr,local_address,sizeof local_address)){close(control);return{};}if(address==local_address){close(control);return{};}std::string greeting;if(!line(control,greeting)||greeting.rfind("220 ",0)!=0){close(control);return{};}std::string user="USER peer@"+std::string(local_address)+"\r\n";if(!send_all(control,user)||!line(control,greeting)||greeting.rfind("230 ",0)!=0){close(control);return{};}auto inventory=peer_inventory(control);send_all(control,"QUIT\r\n");close(control);if(!inventory)return{};return PeerSnapshot{address,inventory->files,inventory->entries};}
std::optional<std::pair<uint32_t,int>> subnet_range(const std::string& value){auto slash=value.find('/');if(slash==std::string::npos)return{};auto address=value.substr(0,slash);int prefix;try{prefix=std::stoi(value.substr(slash+1));}catch(...){return{};}if(prefix<0||prefix>32)return{};in_addr parsed{};if(inet_pton(AF_INET,address.c_str(),&parsed)!=1)return{};uint32_t network=ntohl(parsed.s_addr);uint32_t mask=prefix==0?0:0xffffffffu<<(32-prefix);return std::make_pair(network&mask,prefix);}
void discover_peers(Config c){auto range=subnet_range(c.peer_subnet);if(!range){std::cerr<<"Invalid PEER_SUBNET: "<<c.peer_subnet<<'\n';return;}uint64_t count=uint64_t{1}<<(32-range->second);if(count>4096){std::cerr<<"PEER_SUBNET is too large to scan\n";return;}uint64_t first=range->first,last=first+count;if(range->second<31){++first;--last;}for(uint64_t value=first;value<last;value++){in_addr address{};address.s_addr=htonl(static_cast<uint32_t>(value));char text[INET_ADDRSTRLEN]{};if(!inet_ntop(AF_INET,&address,text,sizeof text))continue;std::string ip=text;auto snapshot=discover_peer(ip,c.port+200);std::lock_guard lock(peers_mutex);if(snapshot){bool changed=!peer_snapshots.contains(ip)||peer_snapshots[ip].files!=snapshot->files;peer_snapshots[ip]=*snapshot;if(changed)std::cout<<"Discovered peer "<<ip<<" with "<<snapshot->files.size()<<" files\n";}else if(peer_snapshots.erase(ip)>0)std::cout<<"Peer disconnected: "<<ip<<'\n';}}
void client(int fd,Config c){
 fs::path current=c.root;bool auth=false;Passive passive;
 send_all(fd,"220 Distributed FTP Server Ready\r\n");
 std::string raw;
 while(line(fd,raw)){
  std::istringstream in(raw);std::string cmd,arg;in>>cmd;std::getline(in,arg);arg=trim(arg);cmd=upper(cmd);
  if(cmd=="USER"){
   if(upper(arg)=="ANONYMOUS"){auth=true;if(!send_all(fd,"230 User logged in, proceed.\r\n"))break;}
   else{send_all(fd,"530 Authentication failed.\r\n");break;}
  }else if(cmd=="QUIT"){send_all(fd,"221 Service closing control connection.\r\n");break;}
  else if(!auth){if(!send_all(fd,"530 Not logged in.\r\n"))break;}
  else if(cmd=="PWD"){if(!send_all(fd,"257 \""+vpath(c,current)+"\" is the current directory.\r\n"))break;}
  else if(cmd=="CWD"||cmd=="CDUP"){
   auto dest=directory(c,current,cmd=="CDUP"?"..":arg);
   if(!dest){if(!send_all(fd,"550 Failed to change directory.\r\n"))break;}
   else{current=*dest;if(!send_all(fd,"250 Directory successfully changed.\r\n"))break;}
  }else if(cmd=="PASV"){
   close_passive(passive);auto opened=open_passive();
   if(!opened){if(!send_all(fd,"425 Can't open passive connection.\r\n"))break;}
   else{passive=*opened;auto reply=passive_reply(fd,passive);if(reply.empty()||!send_all(fd,reply))break;}
  }else if(cmd=="LIST"){
   auto target=path_inside(c,current,arg);
   if(!target||!fs::is_directory(*target)){close_passive(passive);if(!send_all(fd,"550 Failed to list directory.\r\n"))break;}
   else{
    auto data=accept_data(passive);
    if(!data){if(!send_all(fd,"425 Can't open data connection.\r\n"))break;}
    else{auto listing=list_directory(*target);if(!send_all(fd,"150 Opening ASCII mode data connection for directory listing.\r\n")||!send_all(*data,listing)){close(*data);break;}close(*data);if(!send_all(fd,"226 Transfer complete.\r\n"))break;}
   }
  }else if(cmd=="RETR"){
   auto target=path_inside(c,current,arg);
   std::ifstream file;
   if(target&&fs::is_regular_file(*target))file.open(*target,std::ios::binary);
   if(target&&file){
    auto data=accept_data(passive);
    if(!data){if(!send_all(fd,"425 Can't open data connection.\r\n"))break;}
    else if(!send_all(fd,"150 Opening BINARY mode data connection.\r\n")){close(*data);break;}
    else{
     std::array<char,16384> buffer;bool ok=true;
     while(file){file.read(buffer.data(),buffer.size());auto count=file.gcount();if(count>0&&!send_all(*data,std::string_view(buffer.data(),static_cast<size_t>(count)))){ok=false;break;}}
     close(*data);if(!ok||!send_all(fd,"226 Transfer complete.\r\n"))break;
    }
   }else{
    auto relative=requested_relative(c,current,arg);auto owner=relative?peer_for_file(*relative):std::optional<std::string>{};
    if(!owner){close_passive(passive);if(!send_all(fd,"550 Failed to open file.\r\n"))break;}
    else{
     auto data=accept_data(passive);
     if(!data){if(!send_all(fd,"425 Can't open data connection.\r\n"))break;}
     else{
      int peer=authenticated_peer(*owner,c.port+200);
      if(peer<0){close(*data);if(!send_all(fd,"425 Can't open peer data connection.\r\n"))break;}
      else if(!send_all(peer,"PASV\r\n")){close(peer);close(*data);if(!send_all(fd,"425 Can't open peer data connection.\r\n"))break;}
      else{
       auto peer_data=peer_data_socket(peer);
       if(!peer_data){close(peer);close(*data);if(!send_all(fd,"425 Can't open peer data connection.\r\n"))break;}
       else if(!send_all(peer,"RETR "+*relative+"\r\n")){close(*peer_data);close(peer);close(*data);if(!send_all(fd,"425 Can't open peer data connection.\r\n"))break;}
       else{
        std::string response;
        if(!line(peer,response)||response.rfind("150 ",0)!=0){close(*peer_data);close(peer);close(*data);if(!send_all(fd,"550 Failed to open remote file.\r\n"))break;}
        else if(!send_all(fd,"150 Opening BINARY mode data connection.\r\n")){close(*peer_data);close(peer);close(*data);break;}
        else{
         char buffer[16384];bool ok=true;
         while(true){auto count=recv(*peer_data,buffer,sizeof buffer,0);if(count==0)break;if(count<0){if(errno==EINTR)continue;ok=false;break;}if(!send_all(*data,std::string_view(buffer,static_cast<size_t>(count)))){ok=false;break;}}
         close(*peer_data);close(*data);if(ok&&line(peer,response)&&response.rfind("226 ",0)==0)ok=send_all(fd,"226 Transfer complete.\r\n");else ok=false;close(peer);if(!ok)break;
        }
       }
      }
     }
    }
   }
  }else if(cmd.empty()){if(!send_all(fd,"500 Syntax error, command unrecognized.\r\n"))break;}
  else{if(!send_all(fd,"502 Command not implemented.\r\n"))break;}
 }
 close_passive(passive);close(fd);
}
void peer_client(int fd,Config c){
 sockaddr_in remote{};socklen_t length=sizeof remote;
 if(getpeername(fd,(sockaddr*)&remote,&length)<0){close(fd);return;}
 char address[INET_ADDRSTRLEN]{};
 if(!inet_ntop(AF_INET,&remote.sin_addr,address,sizeof address)){close(fd);return;}
 std::string expected="peer@"+std::string(address);fs::path current=c.root;Passive passive;bool auth=false;
 send_all(fd,"220 Distributed FTP Server Ready\r\n");std::string raw;
 while(line(fd,raw)){
  std::istringstream in(raw);std::string cmd,arg;in>>cmd;std::getline(in,arg);arg=trim(arg);cmd=upper(cmd);
  if(!auth){
   if(cmd!="USER"||lower(arg)!=expected){send_all(fd,"530 Authentication failed.\r\n");break;}
   auth=true;if(!send_all(fd,"230 Peer authenticated, proceed.\r\n"))break;
  }else if(cmd=="QUIT"){send_all(fd,"221 Service closing control connection.\r\n");break;}
  else if(cmd=="CWD"||cmd=="CDUP"){
   auto dest=directory(c,current,cmd=="CDUP"?"..":arg);
   if(!dest){if(!send_all(fd,"550 Failed to change directory.\r\n"))break;}
   else{current=*dest;if(!send_all(fd,"250 Directory successfully changed.\r\n"))break;}
  }else if(cmd=="PASV"){
   close_passive(passive);auto opened=open_passive();
   if(!opened){if(!send_all(fd,"425 Can't open passive connection.\r\n"))break;}
   else{passive=*opened;auto reply=passive_reply(fd,passive);if(reply.empty()||!send_all(fd,reply))break;}
  }else if(cmd=="LIST"){
   auto target=path_inside(c,current,arg);
   if(!target||!fs::is_directory(*target)){close_passive(passive);if(!send_all(fd,"550 Failed to list directory.\r\n"))break;}
   else{auto data=accept_data(passive);if(!data){if(!send_all(fd,"425 Can't open data connection.\r\n"))break;}else{auto listing=list_directory(*target);if(!send_all(fd,"150 Opening ASCII mode data connection for directory listing.\r\n")||!send_all(*data,listing)){close(*data);break;}close(*data);if(!send_all(fd,"226 Transfer complete.\r\n"))break;}}
  }else if(cmd=="RETR"){
   auto target=path_inside(c,current,arg);std::ifstream file;
   if(target&&fs::is_regular_file(*target))file.open(*target,std::ios::binary);
   if(!target||!file){close_passive(passive);if(!send_all(fd,"550 Failed to open file.\r\n"))break;}
   else{
    auto data=accept_data(passive);
    if(!data){if(!send_all(fd,"425 Can't open data connection.\r\n"))break;}
    else if(!send_all(fd,"150 Opening BINARY mode data connection.\r\n")){close(*data);break;}
    else{std::array<char,16384> buffer;bool ok=true;while(file){file.read(buffer.data(),buffer.size());auto count=file.gcount();if(count>0&&!send_all(*data,std::string_view(buffer.data(),static_cast<size_t>(count)))){ok=false;break;}}close(*data);if(!ok||!send_all(fd,"226 Transfer complete.\r\n"))break;}
   }
  }else if(cmd.empty()){if(!send_all(fd,"500 Syntax error, command unrecognized.\r\n"))break;}
  else{if(!send_all(fd,"502 Command not implemented.\r\n"))break;}
 }
 close_passive(passive);close(fd);
}
int listener(int port){addrinfo hints{},*list=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;hints.ai_flags=AI_PASSIVE;auto service=std::to_string(port);if(auto e=getaddrinfo(nullptr,service.c_str(),&hints,&list)){std::cerr<<"getaddrinfo: "<<gai_strerror(e)<<'\n';return-1;}int fd=-1;for(auto p=list;p;p=p->ai_next){fd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd<0)continue;int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);if(bind(fd,p->ai_addr,p->ai_addrlen)==0&&listen(fd,BACKLOG)==0)break;close(fd);fd=-1;}freeaddrinfo(list);return fd;}
int main(int argc,char**argv){signal(SIGPIPE,SIG_IGN);if(argc!=2){std::cerr<<"Usage: "<<argv[0]<<" <server.conf>\n";return 1;}auto c=load_config(argv[1]);if(!c)return 1;int l=listener(c->port);if(l<0){std::cerr<<"Unable to bind port "<<c->port<<'\n';return 1;}int peer_port=c->port+200;int peer=listener(peer_port);if(peer<0){close(l);std::cerr<<"Unable to bind peer port "<<peer_port<<'\n';return 1;}std::cout<<"FTP server listening on port "<<c->port<<"\n";std::cout<<"Peer listener listening on port "<<peer_port<<'\n';auto config=*c;std::thread([peer,config]{while(true){sockaddr_storage a{};socklen_t n=sizeof a;int fd=accept(peer,(sockaddr*)&a,&n);if(fd<0){if(errno==EINTR)continue;perror("peer accept");continue;}std::thread(peer_client,fd,config).detach();}}).detach();if(!config.peer_subnet.empty()){std::thread([config]{while(true){discover_peers(config);std::this_thread::sleep_for(std::chrono::seconds(std::max(1,config.peer_interval)));}}).detach();}while(true){sockaddr_storage a{};socklen_t n=sizeof a;int fd=accept(l,(sockaddr*)&a,&n);if(fd<0){if(errno==EINTR)continue;perror("accept");continue;}std::thread(client,fd,*c).detach();}}
