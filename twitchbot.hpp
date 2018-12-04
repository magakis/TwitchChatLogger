#ifndef CPPTRIALS_LAUNCHTWITCHBOT_HPP
#define CPPTRIALS_LAUNCHTWITCHBOT_HPP
#include "mysql.h"

namespace TwitchBot {

     class ConnectionManager {
     public:
          ConnectionManager();
          ~ConnectionManager();
          static DWORD WINAPI RecievingThreadMain(void *param);
          int CreateSqlConnection(const char *user, const char *pass, const char *host, const char *dbname);
          int CreateTwitchConnection(const char *user, const char *auth);
          int SendIrcMsg(const char * msg);
          int StartRecievingData();
     private:
          struct Channel {
               unsigned long hash;
               unsigned short id;
          };

          void InitializeRecievingThread();
          int InsertMessageInDatabase(unsigned short channel, const char *user, const char *msg);
          int CreateChannelInDB(const char *channel);
          inline void SaveMessageToLog(const char *time, const char *channel, const char *name, const char *msg);
          inline void WriteToRawLog(const char *);
          unsigned short GetChannelIdFromDB(const char *channel);
          unsigned short GetChannelId(const char *channel);
          void DisplayTwitchMessageInConsole(const char *tm, const char *chan, const char *user, const char *msg);
               
          inline int InsertInChannelCache(unsigned long hash, unsigned short id);
               
          bool isconnected_sql;
          bool isconnected_twitch;
               
          MYSQL *mysql;
          SOCKET sock;
               
          // Expiremental
          ConnectionManager::Channel known_channels[5];
          //
               
          char internal_buf[512];
          DWORD recv_thread_id;
          HANDLE recv_thread;

          FILE *file[2];               
     };

     class TextHandler {
     public:
          int GetNextIrcLine(char **line, char *buf, unsigned short *start_ofst);   
          bool IsValidUserMsg(const char *);
          void ExtractMsgInfoFromLine(char *line, char **channel, char **name, char **msg);
     };

     unsigned long djb2_Hash(const char* str);
	 
} // namespace TwitchBot

#endif
