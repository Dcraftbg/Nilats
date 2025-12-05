#include "db_context.h"
#include "sqlite3/sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

struct DbContext {
    sqlite3* db;
};

static int sqlite_callback(void* data, int argc, char** argv, char** azColName) {
    (void)data;
    for(int i = 0; i < argc; i++){
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    return 0;
}
static int execute_sql(sqlite3* db, const char* sql){
    char* err_msg = NULL;

    int e = sqlite3_exec(
        db,
        sql,
        sqlite_callback,
        0,
        &err_msg
    );

    if(e != SQLITE_OK){
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    return e;
}
int DbContext_init(DbContext** dbOut, const uint64_t* servers_invited_in, size_t servers_invited_in_len) {
    DbContext* db = calloc(1, sizeof(*db));
    assert(db && "Ran out of memory");
    int e = sqlite3_open("database.db", &db->db);
    if(e != SQLITE_OK) return -1;
    execute_sql(db->db, "create table if not exists server_configs("
                    "guild_id BIGINT UNIQUE PRIMARY KEY,"
                    "welcome_channel BIGINT,"
                    "gulag_id BIGINT,"
                    "admin_role_id BIGINT,"
                    "gulag_role_id BIGINT,"
                    "access_role_id BIGINT"
                ")");
    for(size_t i = 0; i < servers_invited_in_len; ++i) {
        uint64_t guild_id = servers_invited_in[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "create table if not exists guild%lu_camp_laborers(user_id BIGINT UNIQUE PRIMARY KEY, score BIGINT, release_score BIGINT, last_activity_milis BIGINT DEFAULT 0, amount_of_times_sent_to_the_gulag INT DEFAULT 1);", guild_id);
        e = execute_sql(db->db, buf);
        if(e != SQLITE_OK) return -1;
        snprintf(buf, sizeof(buf), "create table if not exists guild%lu_social_credit(user_id BIGINT UNIQUE PRIMARY KEY, social_credit BIGINT);", guild_id);
        e = execute_sql(db->db, buf);
        if(e != SQLITE_OK) return -1;
    }
    *dbOut = db; 
    return 0;
}
void DbContext_free(DbContext* db){
    sqlite3_close(db->db);
    free(db);
}
int DbContext_send_to_gulag(DbContext* db, uint64_t guild_id, uint64_t user_id, uint64_t severity) {
    sqlite3_stmt *stmt;
    int e;

    char buf[512];
    snprintf(buf, sizeof(buf), "update guild%lu_social_credit set social_credit = 0 where user_id = ?\n", guild_id);
    e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    int c = 1;
    e = sqlite3_bind_int64(stmt, c++, user_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_step(stmt);
    if(e != SQLITE_DONE) {
        fprintf(stderr, "DbContext_send_to_gulag: its not ok %d (update)\n", e);
        return -1;
    }
    sqlite3_finalize(stmt);

    snprintf(buf, sizeof(buf), 
        "insert into guild%lu_camp_laborers(user_id, score, release_score, amount_of_times_sent_to_the_gulag)\n"
        "values ( ?, 0, ?, 1 )\n"
        "on conflict(user_id) do update set\n"
        "   score = 0,\n"
        "   release_score = (amount_of_times_sent_to_the_gulag + 1) * ?,\n"
        "   amount_of_times_sent_to_the_gulag = amount_of_times_sent_to_the_gulag + 1\n", guild_id);
 
    e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;

    c = 1;
    e = sqlite3_bind_int64(stmt, c++, user_id);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_int64(stmt, c++, severity);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_int64(stmt, c++, severity);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_step(stmt);
    if(e != SQLITE_DONE) {
        fprintf(stderr, "DbContext_send_to_gulag: its not ok %d (add to camp laborers)\n", e);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}
int DbContext_add_social_credit(DbContext* db, uint64_t guild_id, uint64_t user_id, int64_t amount, SocialCredit* credit) {
    sqlite3_stmt *stmt;
    char buf[256];
    snprintf(buf, sizeof(buf), 
        "insert into guild%lu_social_credit(user_id, social_credit)\n"
        "values ( ?, ? )\n"
        "on conflict(user_id) do update set\n"
        "   social_credit = social_credit + excluded.social_credit\n"
        "returning social_credit\n", guild_id);
    int e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    int c = 1;
    e = sqlite3_bind_int64(stmt, c++, user_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, amount);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_step(stmt);
    if(e != SQLITE_ROW) {
        fprintf(stderr, "DbContext_add_social_credit: its not ok %d\n", e);
        return -1;
    }
    credit->social_credit = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}
int DbContext_get_camp_laborer_from_id(DbContext* db, uint64_t guild_id, uint64_t user_id, CampLaborer* laborer) {
    sqlite3_stmt *stmt;
    char buf[128];
    snprintf(buf, sizeof(buf),
        "select * from guild%lu_camp_laborers\n"
        "where user_id = ?\n", guild_id);
    int e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    int c = 1;
    e = sqlite3_bind_int64(stmt, c++, user_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_step(stmt);
    if(e != SQLITE_ROW) return e == SQLITE_DONE ? 0 : -1;
    c = 1;
    laborer->score = sqlite3_column_int64(stmt, c++);
    laborer->release_score = sqlite3_column_int64(stmt, c++);
    laborer->last_activity_milis = sqlite3_column_int64(stmt, c++);
    laborer->amount_of_times_sent_to_the_gulag = sqlite3_column_int(stmt, c++);
    sqlite3_finalize(stmt);
    return 1;
}
int DbContext_do_camp_activity(DbContext* db, uint64_t guild_id, uint64_t user_id, unsigned int reward, uint64_t last_activity_milis) {
    sqlite3_stmt *stmt;
    char buf[256];
    snprintf(buf, sizeof(buf), 
        "update guild%lu_camp_laborers\n"
        "set\n"
        "   score = score + ?,\n"
        "   last_activity_milis = ?\n"
        "where user_id = ?\n", guild_id);
    int e = sqlite3_prepare_v2(db->db, buf, -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    int c = 1;

    e = sqlite3_bind_int64(stmt, c++, reward);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, last_activity_milis);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, user_id);
    if(e != SQLITE_OK) return -1;

    e = sqlite3_step(stmt);

    if(e != SQLITE_DONE) return -1;
    sqlite3_finalize(stmt);
    return 0;
}
int DbContext_get_server_config(DbContext* db, uint64_t guild_id, ServerConfig* config) {
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db,
        "select welcome_channel, gulag_id, admin_role_id, gulag_role_id, access_role_id from server_configs\n"
        "where guild_id = ?", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    int c = 1;
    e = sqlite3_bind_int64(stmt, c++, guild_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_step(stmt);
    if(e != SQLITE_ROW) return e == SQLITE_DONE ? 0 : -1;
    c = 0;
    config->welcome_channel = sqlite3_column_int64(stmt, c++); 
    config->gulag_id        = sqlite3_column_int64(stmt, c++);
    config->admin_role_id   = sqlite3_column_int64(stmt, c++);
    config->gulag_role_id   = sqlite3_column_int64(stmt, c++);
    config->access_role_id  = sqlite3_column_int64(stmt, c++);
    sqlite3_finalize(stmt);
    return 1;
}
int DbContext_set_config(DbContext* db, uint64_t guild_id, const ServerConfig* config) {
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db,
        "insert into server_configs("
            "guild_id,"
            "welcome_channel,"
            "gulag_id,"
            "admin_role_id,"
            "gulag_role_id,"
            "access_role_id"
        ") values ( ?, ?, ?, ?, ?, ? ) "
        "on conflict(guild_id) do update set "
            "welcome_channel = excluded.welcome_channel,"
            "gulag_id = excluded.gulag_id,"
            "admin_role_id = excluded.admin_role_id,"
            "gulag_role_id = excluded.gulag_role_id,"
            "access_role_id = excluded.access_role_id", -1, &stmt, NULL);
    if(e != SQLITE_OK) return -1;
    int c = 1;
    e = sqlite3_bind_int64(stmt, c++, guild_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, config->welcome_channel);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, config->gulag_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, config->admin_role_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, config->gulag_role_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_bind_int64(stmt, c++, config->access_role_id);
    if(e != SQLITE_OK) return -1;
    e = sqlite3_step(stmt);
    if(e != SQLITE_DONE) return -1;
    sqlite3_finalize(stmt);
    return 0;
}
#if 0
int DbContext_get_social_credit_from_id(DbContext* db, uint64_t user_id, SocialCredit* credit) {
    sqlite3_stmt *stmt;
    int e = sqlite3_prepare_v2(db->db, 
        "select social_credit from social_credit\n"
        "where user_id = ?\n", -1, &stmt, NULL
    );
    if(e != SQLITE_OK) return -1;

    e = sqlite3_bind_int64(stmt, 1, user_id);
    if(e != SQLITE_OK) return -1;

    if(sqlite3_step(stmt) == SQLITE_ROW) {
        credit->social_credit = sqlite3_column_int64(stmt, 0);
    } else {
        credit->social_credit = 0;
    }
    sqlite3_finalize(stmt);
    return 0;
}
#endif
