#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
namespace fs=std::filesystem; constexpr int BACKLOG=32;
struct Config { int port{}; int peer_interval{}; std::string peer_subnet; fs::path root; };
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,b-a+1);}
std::optional<Config> load_config(const std::string& name){
 std::ifstream in(name); if(!in){std::cerr<<"Unable to open configuration file: "<<name<<'\n';return{};}
 Config c; std::string line,port,interval;
 while(std::getline(in,line)){line=trim(line);if(line.empty()||line[0]=='#')continue;auto e=line.find('=');if(e==std::string::npos){std::cerr<<"Invalid configuration line\n";return{};}auto k=trim(line.substr(0,e)),v=trim(line.substr(e+1));if(k=="PORT")port=v;else if(k=="PEER_INTERVAL")interval=v;else if(k=="PEER_SUBNET")c.peer_subnet=v;else std::cerr<<"Warning: ignoring unknown key "<<k<<'\n';}
 try{if(port.empty()||interval.empty())throw std::invalid_argument("missing value");c.port=std::stoi(port);c.peer_interval=std::stoi(interval);}catch(const std::exception&e){std::cerr<<"Invalid configuration: "<<e.what()<<'\n';return{};}
 if(c.port<1||c.port>65535||c.peer_interval<0){std::cerr<<"Invalid PORT or PEER_INTERVAL\n";return{};} c.root=fs::weakly_canonical(fs::current_path()/"db");if(!fs::is_directory(c.root)){std::cerr<<"Database root does not exist: "<<c.root<<'\n';return{};}return c;
}
std::string upper(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::toupper(c);});return s;}
bool send_all(int fd,std::string_view data){size_t n=0;while(n<data.size()){auto r=send(fd,data.data()+n,data.size()-n,0);if(r<0){if(errno==EINTR)continue;return false;}if(!r)return false;n+=(size_t)r;}return true;}
bool line(int fd,std::string& out){out.clear();char c;while(true){auto r=recv(fd,&c,1,0);if(r==0)return false;if(r<0){if(errno==EINTR)continue;return false;}if(c=='\n')break;if(c!='\r')out+=c;if(out.size()>4096)return false;}return true;}
std::optional<fs::path> directory(const Config& c,const fs::path& current,const std::string& arg){fs::path p=arg.empty()?current:fs::path(arg);auto candidate=p.is_absolute()?c.root/p.relative_path():current/p;std::error_code ec;auto normalized=fs::weakly_canonical(candidate,ec);if(ec||!fs::is_directory(normalized))return{};auto root=c.root.generic_string(),text=normalized.generic_string();if(text!=root&&text.rfind(root+"/",0)!=0)return{};return normalized;}
std::string vpath(const Config& c,const fs::path& p){auto r=fs::relative(p,c.root);return r=="."?"/":"/"+r.generic_string();}
void client(int fd,Config c){fs::path current=c.root;bool auth=false;send_all(fd,"220 Distributed FTP Server Ready\r\n");std::string raw;while(line(fd,raw)){std::istringstream in(raw);std::string cmd,arg;in>>cmd;std::getline(in,arg);arg=trim(arg);cmd=upper(cmd);if(cmd=="USER"){if(arg=="anonymous"){auth=true;if(!send_all(fd,"230 User logged in, proceed.\r\n"))break;}else{send_all(fd,"530 Authentication failed.\r\n");break;}}else if(cmd=="QUIT"){send_all(fd,"221 Service closing control connection.\r\n");break;}else if(!auth){if(!send_all(fd,"530 Not logged in.\r\n"))break;}else if(cmd=="PWD"){if(!send_all(fd,"257 \""+vpath(c,current)+"\" is the current directory.\r\n"))break;}else if(cmd=="CWD"||cmd=="CDUP"){auto dest=directory(c,current,cmd=="CDUP"?"..":arg);if(!dest){if(!send_all(fd,"550 Failed to change directory.\r\n"))break;}else{current=*dest;if(!send_all(fd,"250 Directory successfully changed.\r\n"))break;}}else if(cmd=="PASV"||cmd=="LIST"||cmd=="RETR"){if(!send_all(fd,"502 Command not implemented in Milestone 1.\r\n"))break;}else if(cmd.empty()){if(!send_all(fd,"500 Syntax error, command unrecognized.\r\n"))break;}else{if(!send_all(fd,"502 Command not implemented.\r\n"))break;}}close(fd);}
int listener(int port){addrinfo hints{},*list=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;hints.ai_flags=AI_PASSIVE;auto service=std::to_string(port);if(auto e=getaddrinfo(nullptr,service.c_str(),&hints,&list)){std::cerr<<"getaddrinfo: "<<gai_strerror(e)<<'\n';return-1;}int fd=-1;for(auto p=list;p;p=p->ai_next){fd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd<0)continue;int yes=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);if(bind(fd,p->ai_addr,p->ai_addrlen)==0&&listen(fd,BACKLOG)==0)break;close(fd);fd=-1;}freeaddrinfo(list);return fd;}
int main(int argc,char**argv){signal(SIGPIPE,SIG_IGN);if(argc!=2){std::cerr<<"Usage: "<<argv[0]<<" <server.conf>\n";return 1;}auto c=load_config(argv[1]);if(!c)return 1;int l=listener(c->port);if(l<0){std::cerr<<"Unable to bind port "<<c->port<<'\n';return 1;}std::cout<<"FTP server listening on port "<<c->port<<'\n';while(true){sockaddr_storage a{};socklen_t n=sizeof a;int fd=accept(l,(sockaddr*)&a,&n);if(fd<0){if(errno==EINTR)continue;perror("accept");continue;}std::thread(client,fd,*c).detach();}}
