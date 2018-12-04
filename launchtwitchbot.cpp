/*
 * Not sure where this is going
 */

#include <windows.h>
#include <stdlib.h>

#include <iostream>
#include <time.h>

#include "launchtwitchbot.hpp"

using namespace TwitchBot;
using std::cout;
using std::cin;


int main()
{     
     ConnectionManager con_mgr;
     int err = 0;               // not used atm
     /* These are hardcoded for now and login to the bot account */
     err = con_mgr.CreateTwitchConnection("<YOUR_TWICH_NAME>", "<YOUR_AOTH_CODE>");
     err = con_mgr.CreateSqlConnection("DBUSERNAME", "DBPASSWORD", "localhost", "DBNAME");
     
     char msg_buf[512];         // try using the c++ strings? Q^Q
     while ( 1 ) {
          cin.getline(msg_buf, 512);
          con_mgr.SendIrcMsg(msg_buf);
     }
     return 0;
}

int ConnectionManager::CreateTwitchConnection(const char *user, const char *pass)
{
     if (!isconnected_twitch) {
          int err;
          cout << "\rTwitch Bot: Initializing Connection...\n";
          
          // Initialize
          WSADATA wsadata;
          if (err = WSAStartup(MAKEWORD(2,2), &wsadata)) { // returns 0 on SUCCESS
               /* No usable WinSock dll was found */
               cout << "/r[ERROR]: Could not startup (Code: " << err << ")\n";
               return 1;
          }

          sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
          if (sock == INVALID_SOCKET) {
               cout << "/r[ERROR]: Could not create socket (Code: " << WSAGetLastError() << ")\n";
               WSACleanup();
               return 1;
          }

          // Obtain Ip Addresses          
          char twitch_url[] = "irc.chat.twitch.tv";
          unsigned short twitch_port = 6667;
          hostent *hostinf = gethostbyname(twitch_url);
          char twitch_address[3][16] = {}; 
          in_addr addr;
          
          // TODO: Use different address in case of failure
          for (int i = 0; hostinf->h_addr_list[i] != 0 && i < 3; ++i) {
               addr.S_un.S_addr = *(u_long*)hostinf->h_addr_list[i];
               strcpy(twitch_address[i], inet_ntoa(addr));
          }

          // Connect
          sockaddr_in sock_addr = {};
          sock_addr.sin_family = AF_INET;
          sock_addr.sin_addr.s_addr = inet_addr(twitch_address[0]);
          sock_addr.sin_port = htons(twitch_port);
          err = connect(sock, (sockaddr*)&sock_addr, sizeof(sock_addr));

          if (err == SOCKET_ERROR) {
               cout << "\r[ERROR]: Could not connect :C (Code: " << WSAGetLastError() << ")\n";
               if (closesocket(sock)) {
                    cout << "\r[ERROR]: Socket couldn't close?!\n";    
               }
               WSACleanup();
               return 1;
          } else {
               char tmp[512] = "PASS oauth:";
               strcat(tmp, pass);                 
               SendIrcMsg(tmp);

               strcpy(tmp, "NICK ");
               strcat(tmp, user);
               SendIrcMsg(tmp);

               // WARNING: Testing required!
               int bytes = recv(sock, tmp, 512, 0);
               if (bytes > 0 && bytes != SOCKET_ERROR) {
                    tmp[bytes] = '\0';
                    WriteToRawLog(tmp);
                    cout << "\rServer Responded\n";                    
               } else {
                    if (closesocket(sock)) {
                         cout << "\r[ERROR]: Socket couldn't close?!\n";    
                    }
                    WSACleanup();
                    return 1;
               }
          }
          isconnected_twitch = true;
          InitializeRecievingThread();
          cout << "\rConnected to Twitch!\n";
          return 0;
     }
     return 1;
}

int ConnectionManager::CreateSqlConnection(const char *user, const char *pass, const char *host, const char *dbname)
{
     if (!isconnected_sql) {
          mysql = mysql_init(NULL);

          mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");
          mysql_options(mysql, MYSQL_INIT_COMMAND, "SET NAMES utf8mb4");
  
          mysql = mysql_real_connect(mysql, host, user, pass, dbname, NULL, NULL, 0);
          if (mysql == NULL)
          {
               cout << "[Error]: Could not connect to database!\n";
               return 1;
          }
          cout << "Connected to " << dbname << " as " << user << "!\n";
          isconnected_sql = true;
          
          return 0;
     }
     return 1;
}

int ConnectionManager::InsertMessageInDatabase(unsigned short channel, const char *user, const char *raw_msg)
{
          char statement[4096];
          char msg[2048];
          unsigned long lenght;
// Escapes single quote characters in order to be inserted in the db
          for (int i = 0, j = 0; j < 2096 ; ++i,++j) {
               if (raw_msg[i] == '\'') {
                    msg[j] = '\'';
                    msg[++j] = '\'';
                    continue;
               } else if (raw_msg[i] == '\\') {
                    msg[j] = '\\';
                    msg[++j] = '\\';               
               }else {
                    msg[j] = raw_msg[i];
               }
          
               if (msg[j] == '\0')
                    break;
          }
     
          snprintf(statement, 4096, "INSERT INTO test_messages(channel_id, msg_user, msg_text) VALUES(%hu,'%s','%s');", channel, user, msg);
          lenght = strlen(statement);
          return (mysql_real_query(mysql, statement, lenght)); // 0 for success else error
}


int ConnectionManager::StartRecievingData()
{
     cout << "\r~Recieving Thread Started~\n";
     TextHandler text_handler;
     char recv_buf[2046];
     char *line;
     unsigned short leftover_bytes = 0;
     unsigned short processed_bytes = 0;
     int bytes_read;
     int err;

     unsigned short chan_id;

     char *channel;
     char *name;                
     char *msg;
     
     while (bytes_read = recv(sock, recv_buf+leftover_bytes, 2045 - leftover_bytes, 0)) {
          char tmstr[25];
          time_t t = time(NULL);
          struct tm *time = localtime(&t);
          strftime(tmstr, 25, "%Y-%m-%d %H:%M:%S", time);

          processed_bytes = 0;
          leftover_bytes = 0;
          if (bytes_read == SOCKET_ERROR) {
               printf("\r[ERROR]: Failed to recieve data (Code: %i)", WSAGetLastError());
               return 1;
          }
          recv_buf[bytes_read] = '\0'; // There is always space for an extra '\0'

          WriteToRawLog(recv_buf);

          // NOTE: Discards non msg lines..
          while (text_handler.GetNextIrcLine(&line, recv_buf, &processed_bytes)) {
               if (text_handler.IsValidUserMsg(line)) {
                    text_handler.ExtractMsgInfoFromLine(line, &channel, &name, &msg);
                    
                    chan_id = GetChannelId(channel);
                    if (chan_id) {
                         err = InsertMessageInDatabase(chan_id, name, msg);
                         if (err) {
                              cout << "\r[ERROR]:Could not insert message in DB (Code: " << err << ")\n";
                              cout << "\r\tChannel: " << channel << "\n";
                              cout << "\r\tUser: " << name << "\n";
                              cout << "\r\tMessage: " << msg << std::endl;
                         }
                    } else {
                         cout << "[ERROR]: Could not get the Channel ID!\n";
                    }
                    
               } else {
                    // if true, it's just the server trying to play ping-pong with us ^,^'
                    // WARNING: Assumes that NO other message will be sent to us after ping
                    if (line[0] == 'P' &&
                        line[1] == 'I' &&
                        line[2] == 'N' &&
                        line[3] == 'G')
                    {
                         SendIrcMsg("PONG tmi.twitch.tv");
                    }
               }
          }
          leftover_bytes = bytes_read - processed_bytes;
     } 
     cout << "Recieving data loop exited!\n";
     return 0;
}

void ConnectionManager::DisplayTwitchMessageInConsole(const char *tm, const char *chan, const char *user, const char *msg)
{
     cout << tm << " | #" << chan << " | " << user << " : " << msg << std::endl;
}

int ConnectionManager::CreateChannelInDB(const char *channel)
{
     char statement[256];
     snprintf(statement, 256, "INSERT INTO test_channels(channel_name) VALUE(\'%s\')", channel);
     return (mysql_query(mysql, statement)); // 0 for success else error
}

inline int ConnectionManager::InsertInChannelCache(unsigned long hash, unsigned short id)
{
     //TODO: HARDCODED 5
     for (int i = 0; i < 5; ++i) {
          if (known_channels[i].hash == 0) {
               known_channels[i].hash = hash;
               known_channels[i].id = id;
               return 0;
          }
     }
     return 1;
}

unsigned short ConnectionManager::GetChannelId(const char *channel)
{
     int err = 0;
     unsigned short chan_id = 0;
     unsigned long chan_hash = djb2_Hash(channel);

     // search in channel cache
     // TODO: HARDCODED 5
     for (int i = 0; i < 5; ++i) {
          if (known_channels[i].hash == chan_hash) {
               return known_channels[i].id;
          }
     }

     // if not found in the channel cache...
     chan_id = GetChannelIdFromDB(channel);  
     
     if (chan_id) {
          err = InsertInChannelCache(chan_hash, chan_id);
          if (err) {
               // TODO: Probably try to cleanup the known channels to find a spot??
          }
          return chan_id;
     } else {
          err = CreateChannelInDB(channel);
          if (err) {
               return 0;        // Couldn't create channel in db???
          }
          
          chan_id = GetChannelIdFromDB(channel);
          
          if (chan_id) {
               err = InsertInChannelCache(chan_hash, chan_id);
               if (err) {
                    // TODO: Probably try to cleanup the known channels to find a spot??
               }
               return chan_id;
          } else {
               //NOTE: Code should _NEVER_ reach this point
               *(int*)0 = 0;    // Just crash please...Q.Q
               return 0;
          }
     }
}

DWORD WINAPI ConnectionManager::RecievingThreadMain(void *param)
{
     ConnectionManager *obj = (ConnectionManager*)param;
     return obj->StartRecievingData();
}

int ConnectionManager::SendIrcMsg(const char * str)
{
     int bytes_sent;
     int lenght = strlen(str);
     strcpy(internal_buf, str);

     if (lenght > 0) {
          internal_buf[lenght] = '\n'; // replace '\0' with '\n'
          fd_set writefd;
          FD_ZERO(&writefd);
          FD_SET(sock, &writefd);
          select(0, NULL, &writefd, NULL, NULL);
          
          bytes_sent = send(sock, internal_buf, lenght+1, 0); // lenght + 1 cause we also want to send the '\n'

          if (bytes_sent == lenght+1){
               return 0;
          } else if (bytes_sent == SOCKET_ERROR) {
               cout << "\r[ERROR]: Could not send data... (Code: " << WSAGetLastError() << ")\n";
               return 1;
          } else {
               cout << "\rNot all data were sent?! (Code: " << WSAGetLastError() << ")\n";
          }
     }
     return 0;
}

void ConnectionManager::InitializeRecievingThread()
{
     if (recv_thread_id == 0){
          recv_thread = CreateThread(0, 0, RecievingThreadMain, this, 0, &recv_thread_id);
     }
}

unsigned short ConnectionManager::GetChannelIdFromDB(const char *channel)
{
     unsigned short channel_id = 0;
               
     char tmpbuf[128] = "";
     snprintf(tmpbuf, 128, "SELECT channel_id FROM test_channels WHERE channel_name = '%s'", channel);
     mysql_query(mysql, tmpbuf);

     MYSQL_ROW row;
     MYSQL_RES *query_res = mysql_use_result(mysql);
     if (query_res == NULL) {
          cout << "\r[ERROR]:Could not read channel_id query!\n";
          return 0;
     }
     row = mysql_fetch_row(query_res);
     if (row == NULL) {
          mysql_free_result(query_res);
          return 0;
     } else {
          channel_id = (unsigned short)strtoul(row[0], NULL, 0);
          mysql_free_result(query_res);
          return channel_id;
     }
}

inline void ConnectionManager::SaveMessageToLog(const char *time, const char *channel, const char *user, const char *msg)
{
     fprintf(file[0], "%s | #%s | %s : %s\n", time, channel, user, msg);
}

inline void ConnectionManager::WriteToRawLog(const char *str)
{
     fputs(str, file[1]);
}

ConnectionManager::ConnectionManager()
     :isconnected_twitch(0),
      isconnected_sql(0),
      recv_thread(0),
      recv_thread_id(0),
      mysql(0),
      sock(0),
      file{fopen("log.txt", "wb"),
          fopen("raw_log.txt", "wb")},
      //Expiremental
      known_channels{}
{}

ConnectionManager::~ConnectionManager()
{
     if (isconnected_twitch) {
          /* This is Force Termination on the thread.. there should be a more peaceful way..*/
          TerminateThread(recv_thread, 0);
          closesocket(sock);
          WSACleanup();
     }
}

///////////////////////////
/////// TEXTHANDLER
///////////////////////////

bool TextHandler::IsValidUserMsg(const char *line)
{
     /* I don't want to check also for '\n'. I assume it's already checked until this point.
      * If not... I KILL YOU! >.>
      * NOTE(HACK): It only searches for a 'P' and a 'G' 6 characters after that to call it valid (PRIVMSG).
      */
     while (*line != '\0') {
          if (*line == 'P') {
               if (line[6] == 'G') {
                    return true;
               }
          }
          ++line;
     }
     return false;
}

/* Message format is expected to be ":name!junk#channel:msg" */
void TextHandler::ExtractMsgInfoFromLine(char *line, char **channel, char **name, char **msg)
{     
     *name = ++line;
     while (*line != '!') { ++line; }
     *line = '\0';
     while (*line != '#') { ++line; }
     *channel = line+1;         // # not included in channel
     while (*line != ' ') { ++line; }
     *line = '\0';
     while (*line != ':') { ++line; }
     *msg = ++line;
}

int TextHandler::GetNextIrcLine(char **line, char *buf, unsigned short *processed_bytes)
{
     int i = 0;
     int offs = *processed_bytes;
          
     *line = buf+offs;
     // assumes ALL LINES start with a valid char (not \n \r)
     while (buf[i+offs] != '\n' && buf[i+offs] != '\0') { ++i; }

     // assumes that every message ends with a '/n' char
     if (buf[i+offs] == '\n') {
          buf[i+offs-1] = '\0';
          *processed_bytes += i+1;
          return true;
     } else {      // unexpected end probably caused by not recieving the entire message
          *line = NULL;
          return false;
     }
}

// FREE FUNCTIONS THAT NEED TO GET SETTLED

namespace TwitchBot {

     /* TODO:
      * This functions also exists in the Commander class..
      * It should be extracted in a header in order to remove the duplicate code
      */
     unsigned long djb2_Hash(const char* str)
     {
          unsigned long hash = 5381;
          int c;
	  
          while (c = *str++) {
               hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
          }
          return hash;
     }
          
} //namespace TwitchBot
