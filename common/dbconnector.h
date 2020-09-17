#ifndef __DBCONNECTOR__
#define __DBCONNECTOR__

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <memory>

#include <hiredis/hiredis.h>
#include "rediscommand.h"
#include "redisreply.h"
#define EMPTY_NAMESPACE std::string()
 

namespace swss {

class SonicDBConfig
{
public:
    static void initialize(const std::string &file = DEFAULT_SONIC_DB_CONFIG_FILE);
    static std::string getDbInst(const std::string &dbName);
    static int getDbId(const std::string &dbName);
    static std::string getDbSock(const std::string &dbName);
    static std::string getDbHostname(const std::string &dbName);
    static int getDbPort(const std::string &dbName);
    static bool isInit() { return m_init; };

private:
    static constexpr const char *DEFAULT_SONIC_DB_CONFIG_FILE = "/var/run/redis/sonic-db/database_config.json";
    // { instName, { unix_socket_path, {hostname, port} } }
    static std::unordered_map<std::string, std::pair<std::string, std::pair<std::string, int>>> m_inst_info;
    // { dbName, {instName, dbId} }
    static std::unordered_map<std::string, std::pair<std::string, int>> m_db_info;
    static bool m_init;
};

class DBConnector
{
public:
    static constexpr const char *DEFAULT_UNIXSOCKET = "/var/run/redis/redis.sock";

    /*
     * Connect to Redis DB wither with a hostname:port or unix socket
     * Select the database index provided by "db"
     *
     * Timeout - The time in milisecond until exception is been thrown. For
     *           infinite wait, set this value to 0
     */
    DBConnector(int dbId, const std::string &hostname, int port, unsigned int timeout);
    DBConnector(int dbId, const std::string &unixPath, unsigned int timeout);
    DBConnector(const std::string &dbName, unsigned int timeout, bool isTcpConn = false);

    ~DBConnector();

    redisContext *getContext() const;
    int getDbId() const;

    static void select(DBConnector *db);

    /* Create new context to DB */
    DBConnector *newConnector(unsigned int timeout) const;

    /*
     * Assign a name to the Redis client used for this connection
     * This is helpful when debugging Redis clients using `redis-cli client list`
     */
    void setClientName(const std::string& clientName);

    std::string getClientName();

    int64_t del(const std::string &key);

    bool exists(const std::string &key);

    int64_t hdel(const std::string &key, const std::string &field);

    int64_t hdel(const std::string &key, const std::vector<std::string> &fields);

    std::unordered_map<std::string, std::string> hgetall(const std::string &key);

    template <typename OutputIterator>
    void hgetall(const std::string &key, OutputIterator result);

    std::vector<std::string> keys(const std::string &key);

    void set(const std::string &key, const std::string &value);

    void hset(const std::string &key, const std::string &field, const std::string &value);

    template<typename InputIterator>
    void hmset(const std::string &key, InputIterator start, InputIterator stop);

    std::shared_ptr<std::string> get(const std::string &key);

    std::shared_ptr<std::string> hget(const std::string &key, const std::string &field);

    int64_t incr(const std::string &key);

    int64_t decr(const std::string &key);

    int64_t rpush(const std::string &list, const std::string &item);

    std::shared_ptr<std::string> blpop(const std::string &list, int timeout);

private:
    redisContext *m_conn;
    int m_dbId;
};

template<typename OutputIterator>
void DBConnector::hgetall(const std::string &key, OutputIterator result)
{
    RedisCommand sincr;
    sincr.format("HGETALL %s", key.c_str());
    RedisReply r(this, sincr, REDIS_REPLY_ARRAY);

    auto ctx = r.getContext();

    for (unsigned int i = 0; i < ctx->elements; i += 2)
    {
        *result = std::make_pair(ctx->element[i]->str, ctx->element[i+1]->str);
        ++result;
    }
}

template<typename InputIterator>
void DBConnector::hmset(const std::string &key, InputIterator start, InputIterator stop)
{
    RedisCommand shmset;
    shmset.formatHMSET(key, start, stop);
    RedisReply r(this, shmset, REDIS_REPLY_STATUS);
}

}
#endif
