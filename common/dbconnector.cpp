#include <string.h>
#include <stdint.h>
#include <vector>
#include <unistd.h>
#include <errno.h>
#include <system_error>
#include <fstream>
#include "json.hpp"
#include "logger.h"

#include "common/dbconnector.h"
#include "common/redisreply.h"
#include "common/redisapi.h"

using json = nlohmann::json;
using namespace std;
using namespace swss;

void SonicDBConfig::parseDatabaseConfig(const string &file,
                    std::unordered_map<std::string, RedisInstInfo> &inst_entry,
                    std::unordered_map<std::string, SonicDBInfo> &db_entry,
                    std::unordered_map<int, std::string> &separator_entry)
{
    ifstream i(file);
    if (i.good())
    {
        try
        {
            json j;
            i >> j;
            for (auto it = j["INSTANCES"].begin(); it!= j["INSTANCES"].end(); it++)
            {
               string instName = it.key();
               string socket = it.value().at("unix_socket_path");
               string hostname = it.value().at("hostname");
               int port = it.value().at("port");
               inst_entry[instName] = {socket, hostname, port};
            }

            for (auto it = j["DATABASES"].begin(); it!= j["DATABASES"].end(); it++)
            {
               string dbName = it.key();
               string instName = it.value().at("instance");
               int dbId = it.value().at("id");
               string separator = it.value().at("separator");
               db_entry[dbName] = {instName, dbId, separator};

               separator_entry.emplace(dbId, separator);
            }
        }

        catch (domain_error& e)
        {
            SWSS_LOG_ERROR("key doesn't exist in json object, NULL value has no iterator >> %s\n", e.what());
            throw runtime_error("key doesn't exist in json object, NULL value has no iterator >> " + string(e.what()));
        }
        catch (exception &e)
        {
            SWSS_LOG_ERROR("Sonic database config file syntax error >> %s\n", e.what());
            throw runtime_error("Sonic database config file syntax error >> " + string(e.what()));
        }
    }
    else
    {
        SWSS_LOG_ERROR("Sonic database config file doesn't exist at %s\n", file.c_str());
        throw runtime_error("Sonic database config file doesn't exist at " + file);
    }
}

void SonicDBConfig::initializeGlobalConfig(const string &file)
{
    std::string local_file, dir_name, ns_name;
    std::unordered_map<std::string, SonicDBInfo> db_entry;
    std::unordered_map<std::string, RedisInstInfo> inst_entry;
    std::unordered_map<int, std::string> separator_entry;

    SWSS_LOG_ENTER();

    if (m_global_init)
    {
        SWSS_LOG_ERROR("SonicDBConfig Global config is already initialized");
        return;
    }

    ifstream i(file);
    if (i.good())
    {
        local_file = dir_name = std::string();

        // Get the directory name from the file path given as input.
        std::string::size_type pos = file.rfind("/");
        if( pos != std::string::npos)
        {
            dir_name = file.substr(0,pos+1);
        }

        try
        {
            json j;
            i >> j;

            for (auto& element : j["INCLUDES"])
            {
                local_file.append(dir_name);
                local_file.append(element["include"]);

                if(element["namespace"].empty())
                {
                    // If database_config.json is already initlized via SonicDBConfig::initialize
                    // skip initializing it here again.
                    if(m_init)
                    {
                        local_file.clear();
                        continue;
                    }
                    ns_name = EMPTY_NAMESPACE;
                }
                else
                {
                    ns_name = element["namespace"];
                }

                parseDatabaseConfig(local_file, inst_entry, db_entry, separator_entry);
                m_inst_info[ns_name] = inst_entry;
                m_db_info[ns_name] = db_entry;
                m_db_separator[ns_name] = separator_entry;

                if(element["namespace"].empty())
                {
                    // Make regular init also done
                    m_init = true;
                }

                inst_entry.clear();
                db_entry.clear();
                separator_entry.clear();
                local_file.clear();
            }
        }

        catch (domain_error& e)
        {
            SWSS_LOG_ERROR("key doesn't exist in json object, NULL value has no iterator >> %s\n", e.what());
            throw runtime_error("key doesn't exist in json object, NULL value has no iterator >> " + string(e.what()));
        }
        catch (exception &e)
        {
            SWSS_LOG_ERROR("Sonic database config file syntax error >> %s\n", e.what());
            throw runtime_error("Sonic database config file syntax error >> " + string(e.what()));
        }
    }
    else
    {
        SWSS_LOG_ERROR("Sonic database config global file doesn't exist at %s\n", file.c_str());
    }

    // Set it as the global config file is already parsed and init done.
    m_global_init = true;
}

void SonicDBConfig::initialize(const string &file)
{
    std::unordered_map<std::string, SonicDBInfo> db_entry;
    std::unordered_map<std::string, RedisInstInfo> inst_entry;
    std::unordered_map<int, std::string> separator_entry;

    SWSS_LOG_ENTER();

    if (m_init)
    {
        SWSS_LOG_ERROR("SonicDBConfig already initialized");
        throw runtime_error("SonicDBConfig already initialized");
    }

    parseDatabaseConfig(file, inst_entry, db_entry, separator_entry);
    m_inst_info[EMPTY_NAMESPACE] = inst_entry;
    m_db_info[EMPTY_NAMESPACE] = db_entry;
    m_db_separator[EMPTY_NAMESPACE] = separator_entry;

    // Set it as the config file is already parsed and init done.
    m_init = true;
}

void SonicDBConfig::validateNamespace(const string &netns)
{
    SWSS_LOG_ENTER();

    // With valid namespace input and database_global.json is not loaded, ask user to initializeGlobalConfig first
    if(!netns.empty())
    {
        // If global initialization is not done, ask user to initialize global DB Config first.
        if (!m_global_init)
        {
            SWSS_LOG_ERROR("Initialize global DB config first using API SonicDBConfig::initializeGlobalConfig \n");
            throw runtime_error("Initialize global DB config using API SonicDBConfig::initializeGlobalConfig");
        }

        // Check if the namespace is valid, check if this is a key in either of this map
        unordered_map<string, unordered_map<string, RedisInstInfo>>::const_iterator entry = m_inst_info.find(netns);
        if (entry == m_inst_info.end())
        {
            SWSS_LOG_ERROR("Namespace %s is not a valid namespace name in config file\n", netns.c_str());
            throw runtime_error("Namespace " + netns + " is not a valid namespace name in config file");
        }
    }
}

string SonicDBConfig::getDbInst(const string &dbName, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_db_info[netns].at(dbName).instName;
}

int SonicDBConfig::getDbId(const string &dbName, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_db_info[netns].at(dbName).dbId;
}

string SonicDBConfig::getSeparator(const string &dbName, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_db_info[netns].at(dbName).separator;
}

string SonicDBConfig::getSeparator(int dbId, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_db_separator[netns].at(dbId);
}

string SonicDBConfig::getSeparator(const DBConnector* db)
{
    if (!db)
    {
        throw std::invalid_argument("db cannot be null");
    }

    string dbName = db->getDbName();
    string netns = db->getNamespace();
    if (dbName.empty())
    {
        return getSeparator(db->getDbId(), netns);
    }
    else
    {
        return getSeparator(dbName, netns);
    }
}

string SonicDBConfig::getDbSock(const string &dbName, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_inst_info[netns].at(getDbInst(dbName)).unixSocketPath;
}

string SonicDBConfig::getDbHostname(const string &dbName, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_inst_info[netns].at(getDbInst(dbName)).hostname;
}

int SonicDBConfig::getDbPort(const string &dbName, const string &netns)
{
    if (!m_init)
        initialize(DEFAULT_SONIC_DB_CONFIG_FILE);
    validateNamespace(netns);
    return m_inst_info[netns].at(getDbInst(dbName)).port;
}

vector<string> SonicDBConfig::getNamespaces()
{
    vector<string> list;

    if (!m_global_init)
        initializeGlobalConfig();

    // This API returns back non-empty namespaces.
    for (auto it = m_inst_info.cbegin(); it != m_inst_info.cend(); ++it) {
        if(!((it->first).empty()))
            list.push_back(it->first);
    }

    return list;
}

std::vector<std::string> SonicDBConfig::getDbList(const std::string &netns)
{
    if (!m_init)
    {
        initialize();
    }
    validateNamespace(netns);

    std::vector<std::string> dbNames;
    for (auto& imap: m_db_info.at(netns))
    {
        dbNames.push_back(imap.first);
    }
    return dbNames;
}

constexpr const char *SonicDBConfig::DEFAULT_SONIC_DB_CONFIG_FILE;
constexpr const char *SonicDBConfig::DEFAULT_SONIC_DB_GLOBAL_CONFIG_FILE;
unordered_map<string, unordered_map<string, RedisInstInfo>> SonicDBConfig::m_inst_info;
unordered_map<string, unordered_map<string, SonicDBInfo>> SonicDBConfig::m_db_info;
unordered_map<string, unordered_map<int, string>> SonicDBConfig::m_db_separator;
bool SonicDBConfig::m_init = false;
bool SonicDBConfig::m_global_init = false;

constexpr const char *RedisContext::DEFAULT_UNIXSOCKET;

RedisContext::~RedisContext()
{
    redisFree(m_conn);
}

RedisContext::RedisContext()
{
}

RedisContext::RedisContext(const RedisContext &other)
{
    auto octx = other.getContext();
    const char *unixPath = octx->unix_sock.path;
    if (unixPath)
    {
        initContext(unixPath, *octx->timeout);
    }
    else
    {
        initContext(octx->tcp.host, octx->tcp.port, *octx->timeout);
    }
}

RedisContext::RedisContext(const string& hostname, int port,
                         unsigned int timeout)
{
    struct timeval tv = {0, (suseconds_t)timeout * 1000};
    initContext(hostname.c_str(), port, tv);
}

RedisContext::RedisContext(const string& unixPath, unsigned int timeout)
{
    struct timeval tv = {0, (suseconds_t)timeout * 1000};
    initContext(unixPath.c_str(), tv);
}

void RedisContext::initContext(const char *host, int port, const timeval& tv)
{
    m_conn = redisConnectWithTimeout(host, port, tv);

    if (m_conn->err)
        throw system_error(make_error_code(errc::address_not_available),
                           "Unable to connect to redis");
}

void RedisContext::initContext(const char *path, const timeval &tv)
{
    m_conn = redisConnectUnixWithTimeout(path, tv);

    if (m_conn->err)
        throw system_error(make_error_code(errc::address_not_available),
                           "Unable to connect to redis (unix-socket)");
}

redisContext *RedisContext::getContext() const
{
    return m_conn;
}

void RedisContext::setContext(redisContext *ctx)
{
    m_conn = ctx;
}

void RedisContext::setClientName(const string& clientName)
{
    string command("CLIENT SETNAME ");
    command += clientName;

    RedisReply r(this, command, REDIS_REPLY_STATUS);
    r.checkStatusOK();
}

string RedisContext::getClientName()
{
    string command("CLIENT GETNAME");

    RedisReply r(this, command);

    auto ctx = r.getContext();
    if (ctx->type == REDIS_REPLY_STRING)
    {
        return r.getReply<std::string>();
    }
    else
    {
        if (ctx->type != REDIS_REPLY_NIL)
            SWSS_LOG_ERROR("Unable to obtain Redis client name");

        return "";
    }
}

void DBConnector::select(DBConnector *db)
{
    string select("SELECT ");
    select += to_string(db->getDbId());

    RedisReply r(db, select, REDIS_REPLY_STATUS);
    r.checkStatusOK();

    std::string luaScript = loadLuaScript("redis_multi.lua");
    db->m_shaRedisMulti = loadRedisScript(db, luaScript);
}

DBConnector::DBConnector(const DBConnector &other)
    : RedisContext(other)
    , m_dbId(other.m_dbId)
    , m_namespace(other.m_namespace)
{
    select(this);
}

DBConnector::DBConnector(int dbId, const RedisContext& ctx)
    : RedisContext(ctx)
    , m_dbId(dbId)
    , m_namespace(EMPTY_NAMESPACE)
{
    select(this);
}

DBConnector::DBConnector(int dbId, const string& hostname, int port,
                         unsigned int timeout) :
    RedisContext(hostname, port, timeout),
    m_dbId(dbId),
    m_namespace(EMPTY_NAMESPACE)
{
    select(this);
}

DBConnector::DBConnector(int dbId, const string& unixPath, unsigned int timeout) :
    RedisContext(unixPath, timeout),
    m_dbId(dbId),
    m_namespace(EMPTY_NAMESPACE)
{
    select(this);
}

DBConnector::DBConnector(const string& dbName, unsigned int timeout, bool isTcpConn, const string& netns)
    : m_dbId(SonicDBConfig::getDbId(dbName, netns))
    , m_dbName(dbName)
    , m_namespace(netns)
{
    struct timeval tv = {0, (suseconds_t)timeout * 1000};

    if (isTcpConn)
    {
        initContext(SonicDBConfig::getDbHostname(dbName, netns).c_str(), SonicDBConfig::getDbPort(dbName, netns), tv);
    }
    else
    {
        initContext(SonicDBConfig::getDbSock(dbName, netns).c_str(), tv);
    }

    select(this);
}

DBConnector::DBConnector(const string& dbName, unsigned int timeout, bool isTcpConn)
    : DBConnector(dbName, timeout, isTcpConn, EMPTY_NAMESPACE)
{
    // Empty contructor
}

int DBConnector::getDbId() const
{
    return m_dbId;
}

string DBConnector::getDbName() const
{
    return m_dbName;
}

void DBConnector::setNamespace(const string& netns)
{
    m_namespace = netns;
}

string DBConnector::getNamespace() const
{
    return m_namespace;
}

DBConnector *DBConnector::newConnector(unsigned int timeout) const
{
    DBConnector *ret;

    if (getContext()->connection_type == REDIS_CONN_TCP)
        ret = new DBConnector(getDbId(),
                               getContext()->tcp.host,
                               getContext()->tcp.port,
                               timeout);
    else
        ret = new DBConnector(getDbId(),
                               getContext()->unix_sock.path,
                               timeout);

    ret->m_dbName = m_dbName;
    ret->setNamespace(getNamespace());

    return ret;
}

int64_t DBConnector::del(const string &key)
{
    RedisCommand sdel;
    sdel.format("DEL %s", key.c_str());
    RedisReply r(this, sdel, REDIS_REPLY_INTEGER);
    return r.getContext()->integer;
}

bool DBConnector::exists(const string &key)
{
    RedisCommand rexists;
    if (key.find_first_of(" \t") != string::npos)
    {
        SWSS_LOG_ERROR("EXISTS failed, invalid space or tab in single key: %s", key.c_str());
        throw runtime_error("EXISTS failed, invalid space or tab in single key");
    }
    rexists.format("EXISTS %s", key.c_str());
    RedisReply r(this, rexists, REDIS_REPLY_INTEGER);
    return (r.getContext()->integer > 0);
}

int64_t DBConnector::hdel(const string &key, const string &field)
{
    RedisCommand shdel;
    shdel.format("HDEL %s %s", key.c_str(), field.c_str());
    RedisReply r(this, shdel, REDIS_REPLY_INTEGER);
    return r.getContext()->integer;
}

int64_t DBConnector::hdel(const std::string &key, const std::vector<std::string> &fields)
{
    RedisCommand shdel;
    shdel.formatHDEL(key, fields);
    RedisReply r(this, shdel, REDIS_REPLY_INTEGER);
    return r.getContext()->integer;
}

void DBConnector::hset(const string &key, const string &field, const string &value)
{
    RedisCommand shset;
    shset.format("HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    RedisReply r(this, shset, REDIS_REPLY_INTEGER);
}

void DBConnector::set(const string &key, const string &value)
{
    RedisCommand sset;
    sset.format("SET %s %s", key.c_str(), value.c_str());
    RedisReply r(this, sset, REDIS_REPLY_STATUS);
}

void DBConnector::config_set(const std::string &key, const std::string &value)
{
    RedisCommand sset;
    sset.format("CONFIG SET %s %s", key.c_str(), value.c_str());
    RedisReply r(this, sset, REDIS_REPLY_STATUS);
}

unordered_map<string, string> DBConnector::hgetall(const string &key)
{
    unordered_map<string, string> map;
    hgetall(key, std::inserter(map, map.end()));
    return map;
}

vector<string> DBConnector::keys(const string &key)
{
    RedisCommand skeys;
    skeys.format("KEYS %s", key.c_str());
    RedisReply r(this, skeys, REDIS_REPLY_ARRAY);

    auto ctx = r.getContext();

    vector<string> list;
    for (unsigned int i = 0; i < ctx->elements; i++)
        list.emplace_back(ctx->element[i]->str);

    return list;
}

int64_t DBConnector::incr(const string &key)
{
    RedisCommand sincr;
    sincr.format("INCR %s", key.c_str());
    RedisReply r(this, sincr, REDIS_REPLY_INTEGER);
    return r.getContext()->integer;
}

int64_t DBConnector::decr(const string &key)
{
    RedisCommand sdecr;
    sdecr.format("DECR %s", key.c_str());
    RedisReply r(this, sdecr, REDIS_REPLY_INTEGER);
    return r.getContext()->integer;
}

shared_ptr<string> DBConnector::get(const string &key)
{
    RedisCommand sget;
    sget.format("GET %s", key.c_str());
    RedisReply r(this, sget);
    auto reply = r.getContext();

    if (reply->type == REDIS_REPLY_NIL)
    {
        return shared_ptr<string>(NULL);
    }

    if (reply->type == REDIS_REPLY_STRING)
    {
        shared_ptr<string> ptr(new string(reply->str));
        return ptr;
    }

    throw runtime_error("GET failed, memory exception");
}

shared_ptr<string> DBConnector::hget(const string &key, const string &field)
{
    RedisCommand shget;
    shget.format("HGET %s %s", key.c_str(), field.c_str());
    RedisReply r(this, shget);
    auto reply = r.getContext();

    if (reply->type == REDIS_REPLY_NIL)
    {
        return shared_ptr<string>(NULL);
    }

    if (reply->type == REDIS_REPLY_STRING)
    {
        shared_ptr<string> ptr(new string(reply->str));
        return ptr;
    }

    SWSS_LOG_ERROR("HGET failed, reply-type: %d, %s: %s", reply->type, key.c_str(), field.c_str());
    throw runtime_error("HGET failed, unexpected reply type, memory exception");
}

int64_t DBConnector::rpush(const string &list, const string &item)
{
    RedisCommand srpush;
    srpush.format("RPUSH %s %s", list.c_str(), item.c_str());
    RedisReply r(this, srpush, REDIS_REPLY_INTEGER);
    return r.getContext()->integer;
}

shared_ptr<string> DBConnector::blpop(const string &list, int timeout)
{
    RedisCommand sblpop;
    sblpop.format("BLPOP %s %d", list.c_str(), timeout);
    RedisReply r(this, sblpop);
    auto reply = r.getContext();

    if (reply->type == REDIS_REPLY_NIL)
    {
        return shared_ptr<string>(NULL);
    }

    if (reply->type == REDIS_REPLY_STRING)
    {
        shared_ptr<string> ptr(new string(reply->str));
        return ptr;
    }

    throw runtime_error("GET failed, memory exception");
}

void DBConnector::subscribe(const std::string &pattern)
{
    std::string s("SUBSCRIBE ");
    s += pattern;
    RedisReply r(this, s, REDIS_REPLY_ARRAY);
}

void DBConnector::psubscribe(const std::string &pattern)
{
    std::string s("PSUBSCRIBE ");
    s += pattern;
    RedisReply r(this, s, REDIS_REPLY_ARRAY);
}

int64_t DBConnector::publish(const string &channel, const string &message)
{
    RedisCommand publish;
    publish.format("PUBLISH %s %s", channel.c_str(), message.c_str());
    RedisReply r(this, publish, REDIS_REPLY_INTEGER);
    return r.getReply<long long int>();
}

void DBConnector::hmset(const std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>& multiHash)
{
    SWSS_LOG_ENTER();

    json j;

    // pack multi hash to json (takes bout 70 ms for 10k to construct)
    for (const auto& kvp: multiHash)
    {
        json o;

        for (const auto &item: kvp.second)
        {
            o[std::get<0>(item)] = std::get<1>(item);
        }

        j[kvp.first] = o;
    }

    std::string strJson = j.dump();

    RedisCommand command;
    command.format(
        "EVALSHA %s 1 %s %s",
        m_shaRedisMulti.c_str(),
        strJson.c_str(),
        "mhset");

    RedisReply r(this, command, REDIS_REPLY_NIL);
}

void DBConnector::hdel(const std::vector<std::string>& keys)
{
    SWSS_LOG_ENTER();

    json j = json::array();

    for (const auto& key: keys)
    {
        j.push_back(key);
    }

    std::string strJson = j.dump();

    RedisCommand command;
    command.format(
        "EVALSHA %s 1 %s %s",
        m_shaRedisMulti.c_str(),
        strJson.c_str(),
        "mdel");

    RedisReply r(this, command, REDIS_REPLY_NIL);
}
