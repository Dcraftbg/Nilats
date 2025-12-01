#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct DbContext DbContext;

int DbContext_init(DbContext** dbOut, const uint64_t* servers_invited_in, size_t servers_invited_in_len);
void DbContext_free(DbContext* db);

typedef struct {
    uint64_t welcome_channel;
    uint64_t gulag_id;
    uint64_t admin_role_id;
    uint64_t gulag_role_id;
    uint64_t access_role_id;
} ServerConfig;
int DbContext_get_server_config(DbContext* db, uint64_t guild_id, ServerConfig* config);
int DbContext_set_config(DbContext* db, uint64_t guild_id, const ServerConfig* config);

typedef struct {
    uint64_t score;
    uint64_t release_score;
    uint64_t last_activity_milis;
    int amount_of_times_sent_to_the_gulag;
} CampLaborer;
// 0  not found
// 1  found
// -1 error
int DbContext_get_camp_laborer_from_id(DbContext* db, uint64_t guild_id, uint64_t user_id, CampLaborer* laborer);
int DbContext_do_camp_activity(DbContext* db, uint64_t guild_id, uint64_t user_id, unsigned int reward, uint64_t time_ms);
int DbContext_send_to_gulag(DbContext* db, uint64_t guild_id, uint64_t user_id, uint64_t severity);

typedef struct {
    int64_t social_credit;
} SocialCredit;
int DbContext_add_social_credit(DbContext* db, uint64_t guild_id, uint64_t user_id, int64_t amount, SocialCredit* credit);

