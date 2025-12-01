#include "../token.h"
#include <string.h>
#include <discord.h>
#include <log.h>
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include "db_context.h"
#define ARRAY_LEN(n) (sizeof(n)/sizeof(*n))

#include <time.h>
uint64_t time_unix_milis(void) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

DbContext* db = NULL; 
void on_ready(struct discord *client, const struct discord_ready *event) {
    (void)client;
    log_info("Logged in as %s!", event->user->username);
}
char* ltrim(char* str) {
    while(isspace(*str)) str++;
    return str;
}
static inline bool word_cstr_eq(const char* word, size_t len, const char* str) {
    return strlen(str) == len && strncasecmp(word, str, len) == 0;
}
void send_to_gulag(struct discord *client, const char* username_dbg, uint64_t id, uint64_t guild_id, uint64_t gulag_channel, uint64_t gulag_role, uint64_t access_role, uint64_t severity) {
    log_info("%s was sent to the gulag.", username_dbg);

    const char* gulag_reply[] = {
        "Welcome to the gulag, comrad <@%lu>",
        "You have been naughty <@%lu>, I know what you did. Now you have to pay for your crimes",
        "Comrad <@%lu>, the regime doesn't like some of the things you said",
        "You'll have to be `corrected` comrad <@%lu>",
        "Comrad <@%lu>, I never forget",
        "Nilats does not forgive. Nilats does not forget. Report to Re-education Block 9, <@%lu>",
        "You have been unmasked as an enemy of the people comrad <@%lu>",
        "Comrad <@%lu>, you will under-go 3 years of pickaxe duty to repay back for your sins against the party",
        "Big Nilats loves you more than you can comprehend comrad <@%lu>.\nOut of pure love Nilats has scheduled you a mandatory vacation."
    };
    char buf[1024];
    snprintf(buf, ARRAY_LEN(buf), gulag_reply[rand() % ARRAY_LEN(gulag_reply)], id);
    discord_create_message(client, gulag_channel, &(struct discord_create_message) {
        .content = buf, 
        .message_reference = NULL,
    }, NULL);

    discord_remove_guild_member_role(client, guild_id, id, access_role, NULL, NULL);
    discord_add_guild_member_role(client, guild_id, id, gulag_role, NULL, NULL);
    if(DbContext_send_to_gulag(db, guild_id, id, severity) < 0) {
        log_error("Failed to send %s to the gulag :(", username_dbg);
    }
}
bool has_role(struct snowflakes* roles, uint64_t role_id) {
    for(int i = 0; i < roles->size; ++i) {
        if(roles->array[i] == role_id) return true;
    }
    return false;
}
void on_message(struct discord *client, const struct discord_message *event) {
    ServerConfig server_config = { 0 };
    if(DbContext_get_server_config(db, event->guild_id, &server_config) <= 0) {
        log_error("Server not in configs!");
        return;
    }
    char* msg = ltrim(event->content);

    const uint64_t MS_IN_SECOND = 1000;
    const uint64_t MS_IN_MINUTE = MS_IN_SECOND * 60;
    const uint64_t MS_IN_HOUR   = MS_IN_MINUTE * 60;
    const uint64_t TIMEOUT_BETWEEN_ACTIVITIES_MS = 12 * MS_IN_HOUR;
    if(*msg == '!') {
        msg++;
        char *cmd_str = msg,
             *cmd_str_end;
        while(*msg && !isspace(*msg)) msg++;
        cmd_str_end = msg;
        enum {
            CMD_HELLO,
            CMD_PEEL,
            CMD_EVAPORATE,
            CMD_PARDON,
            CMD_COUNT
        };
        char* cmds[] = {
            [CMD_HELLO] = "hello",
            [CMD_PEEL] = "peel",
            [CMD_EVAPORATE] = "evaporate",
            [CMD_PARDON] = "pardon",
        };
        static_assert(ARRAY_LEN(cmds) == CMD_COUNT, "Update cmds");
        int cmd = -1;
        for(size_t i = 0; i < ARRAY_LEN(cmds); ++i) {
            size_t cmd_len = strlen(cmds[i]);
            if(cmd_len == (size_t)(cmd_str_end - cmd_str) && memcmp(cmd_str, cmds[i], cmd_len) == 0) {
                cmd = i;
                break;
            }
        }
        if(cmd < 0) {
            log_warn("Unknown cmd `%.*s` issued by %s", cmd_str_end - cmd_str, cmd_str, event->author->username);
            return;
        }
        switch(cmd) {
        case CMD_HELLO: {
            char msg_buf[256];
            if(*msg) {
                snprintf(msg_buf, sizeof(msg_buf), "Hello comrad %s", ltrim(msg));
            } else {
                snprintf(msg_buf, sizeof(msg_buf), "Hello comrad <@%lu>", event->author->id);
            }
            discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                .content = msg_buf, 
                .message_reference = &(struct discord_message_reference) {
                    .message_id = event->id,
                    .channel_id = event->channel_id,
                    .guild_id = event->guild_id
                },
            }, NULL);
        } break;
        case CMD_PEEL: {
            if(event->channel_id != server_config.gulag_id) {
                log_warn("Somebody in another channel tried to peel a tato");
                break;
            }
            CampLaborer laborer;
            int e = DbContext_get_camp_laborer_from_id(db, event->guild_id, event->author->id, &laborer);
            if(e <= 0) {
                if(e == 0) log_warn("Somebody outside the gulag tried to peel a tato");
                else {
                    log_error("DB error on lookup for laborer");
                }
                break;
            }
            uint64_t now = time_unix_milis();

            if(laborer.last_activity_milis > now) {
                log_error("Bogus amogus time? How did someone work in the future??");
                break;
            }
            if(now - laborer.last_activity_milis < TIMEOUT_BETWEEN_ACTIVITIES_MS) {
                log_info("Guy ova hea ovaworkin' himsel' %s", event->author->username);
                uint64_t left_ms = TIMEOUT_BETWEEN_ACTIVITIES_MS - (now - laborer.last_activity_milis);
                char buf[1024];

                snprintf(buf, sizeof(buf), "Slow down there comrad. We don't want you dying from exhaustion. Wait another %lu.%lu hours\n", left_ms / MS_IN_HOUR, (left_ms % MS_IN_HOUR) * 100 / MS_IN_HOUR);
                discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                    .content = buf, 
                    .message_reference = &(struct discord_message_reference) {
                        .message_id = event->id,
                        .channel_id = event->channel_id,
                        .guild_id = event->guild_id
                    },
                }, NULL);
            } else {
                int v = rand() % 4;
                if(v == 0) {
                    log_info("%s failed to peel the tato :(", event->author->username);
                    if(DbContext_do_camp_activity(db, event->guild_id, event->author->id, 0, now) < 0) {
                        log_error("DB Failed to do activity :(");
                        break;
                    }
                    discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                        .content = "Watch out there comrad, you cut yourself!", 
                        .message_reference = &(struct discord_message_reference) {
                            .message_id = event->id,
                            .channel_id = event->channel_id,
                            .guild_id = event->guild_id
                        },
                    }, NULL);
                } else {
                    log_info("%s cut a tato :)", event->author->username);
                    const unsigned int reward = 1;
                    if(DbContext_do_camp_activity(db, event->guild_id, event->author->id, reward, now) < 0) {
                        log_error("DB Failed to do activity :(");
                        break;
                    }
                    if(laborer.score + reward >= laborer.release_score) {
                        discord_remove_guild_member_role(client, event->guild_id, event->author->id, server_config.gulag_role_id, NULL, NULL);
                        discord_add_guild_member_role(client, event->guild_id, event->author->id, server_config.access_role_id, NULL, NULL);
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Comrad %s has escaped the gulag.", event->author->username);
                        discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                            .content = buf 
                        }, NULL);
                        // discord_add_guild_member_role(client, event->guild_id, id)
                        // log_info("TODO: Free this guy from the labor camp!");
                        break;
                    }
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Big Nilats is proud of your valient effort. You're at %lu/%lu score. See you back in a couple of hours.", laborer.score + reward, laborer.release_score);
                    discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                        .content = buf, 
                        .message_reference = &(struct discord_message_reference) {
                            .message_id = event->id,
                            .channel_id = event->channel_id,
                            .guild_id = event->guild_id
                        },
                    }, NULL);
                }
            }
        } break;
        case CMD_EVAPORATE:
        case CMD_PARDON: {
            if(!has_role(event->member->roles, server_config.admin_role_id)) return;
            uint64_t next_id = 0;
            msg = ltrim(msg);
            if(*msg != '<' || msg[1] != '@' || !isdigit(msg[2])) goto invalid_format;
            msg += 2;
            while(isdigit(*msg)) {
                next_id *= 10;
                next_id += *(msg++) - '0';
            }
            if(*msg != '>') goto invalid_format;
            log_info("%s has %s %lu", event->author->username, cmd == CMD_PARDON ? "pardoned" : "evaporated", next_id);
            if(cmd == CMD_PARDON) {
                discord_add_guild_member_role(client, event->guild_id, next_id, server_config.access_role_id, NULL, NULL);
                discord_remove_guild_member_role(client, event->guild_id, next_id, server_config.gulag_role_id, NULL, NULL);
            } else {
                send_to_gulag(client, "<Evaportated person>", next_id, event->guild_id, server_config.gulag_id, server_config.gulag_role_id, server_config.access_role_id, 1);
            }
            discord_delete_message(client, event->channel_id, event->id, NULL, NULL);
            break;
        invalid_format:
            discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                .content = "Invalid format for evaporate/pardon. I expect an @<whoever> after it.", 
                .message_reference = &(struct discord_message_reference) {
                    .message_id = event->id,
                    .channel_id = event->channel_id,
                    .guild_id = event->guild_id
                },
            }, NULL);
        } break;
        }
    } else if(event->channel_id != server_config.gulag_id){
        int64_t social_credit_value = 0;
        bool said_bad = false,
             said_good = false,
             said_ninja = false;

        char* bad_synonyms[] = {
            "horrible",
            "bad",
            "terrible",
            "stupid",
            "dumb",
        };
        char* good_synonyms[] = {
            "good",
            "best",
            "awesome",
            "amazing",
            "fantastic",
            "fire",
            "great",
            "better"
        };
        struct {
            char* name;
            int score;
            bool is_mentioned;
        } mention_figure[] = {
            { "diego" , 10, 0 },
            { "terry" , 8 , 0 },
            { "davis" , 8 , 0 },
            { "hugo"  , 7 , 0 },
            { "elias" , 7 , 0 },
            { "nilats", 5 , 0 },
            { "kap"   ,-5 , 0 },
            { "plane" ,-3 , 0 },
        };
        int nuance = 0;
        bool mentions_gulag = false;
        for(;;) {
            while(isspace(*msg) || ispunct(*msg)) msg++;
            if(*msg == '\0') break; 
            char* word_start = msg;
            while(*msg && !isspace(*msg) && !ispunct(*msg)) msg++;
            size_t word_len = msg - word_start;

            if(word_cstr_eq(word_start, word_len, "gulag")) {
                mentions_gulag = true;
            }
            if(word_cstr_eq(word_start, word_len, "nigga") ||
               word_cstr_eq(word_start, word_len, "nigger"))
            {
                said_ninja = true;
            }
            for(size_t i = 0; i < ARRAY_LEN(good_synonyms); ++i) {
                if(word_cstr_eq(word_start, word_len, good_synonyms[i])) {
                    said_good = true;
                    break;
                }
            }
            for(size_t i = 0; i < ARRAY_LEN(bad_synonyms); ++i) {
                if(word_cstr_eq(word_start, word_len, bad_synonyms[i])) {
                    said_bad = true;
                    break;
                }
            }
            for(size_t i = 0; i < ARRAY_LEN(mention_figure); ++i) {
                if(word_cstr_eq(word_start, word_len, mention_figure[i].name)) {
                    mention_figure[i].is_mentioned = true;
                    break;
                }
            }
        }
        CampLaborer laborer;
        if(mentions_gulag && DbContext_get_camp_laborer_from_id(db, event->guild_id, event->author->id, &laborer) == 1) {
            log_info("A guy mentioned the gulag after coming back. Guess you're going back %s comrad. Original Message: `%s`", event->author->username, event->content);
            discord_delete_message(client, event->channel_id, event->id, NULL, NULL);
            send_to_gulag(client, event->author->username, event->author->id, event->guild_id, server_config.gulag_id, server_config.gulag_role_id, server_config.access_role_id, 1);
            return;
        }
        if(said_ninja) {
            log_info("Said the N-word: %s", event->content);
            discord_delete_message(client, event->channel_id, event->id, NULL, NULL);
            char buf[256];
            snprintf(buf, sizeof(buf), "Big Nilats doesn't tolerate such language comrad <@%lu>", event->author->id);
            discord_create_message(client, event->channel_id, &(struct discord_create_message) {
                .content = buf, 
                .message_reference = NULL,
            }, NULL);
            social_credit_value -= 50;
        }
        if(said_good) nuance++;
        if(said_bad) nuance--;
        for(size_t i = 0; i < ARRAY_LEN(mention_figure); ++i) {
            if(mention_figure[i].is_mentioned) {
                social_credit_value += nuance * mention_figure[i].score;
            }
        }
        if(social_credit_value) {
            SocialCredit credit = { 0 };
            if(DbContext_add_social_credit(db, event->guild_id, event->author->id, social_credit_value, &credit) < 0) {
                log_error("Adding social credit failed!");
                return;
            }
            log_info("%s is at %ld social credit", event->author->username, credit.social_credit);
            if(credit.social_credit < -100) {
                uint64_t severity = (-(credit.social_credit + 100)) / 20 + 1;
                send_to_gulag(client, event->author->username, event->author->id, event->guild_id, server_config.gulag_id, server_config.gulag_role_id, server_config.access_role_id, severity);
            }
        }
    }
}
void on_join(struct discord *client, const struct discord_guild_member *member) {
    ServerConfig server_config = { 0 };
    if(DbContext_get_server_config(db, member->guild_id, &server_config) <= 0) return;

    CampLaborer laborer;
    if(DbContext_get_camp_laborer_from_id(db, member->guild_id, member->user->id, &laborer) > 0 && laborer.score < laborer.release_score) {
        send_to_gulag(client, member->user->username, member->user->id, member->guild_id, server_config.gulag_id, server_config.gulag_role_id, server_config.access_role_id, 2);
        char buf[256];
        snprintf(buf, sizeof(buf), "You thought you can escape me comrad <@%lu>", member->user->id);
        discord_create_message(client, server_config.gulag_id, &(struct discord_create_message) {
            .content = buf, 
            .message_reference = NULL,
        }, NULL);
        return;
    }
    discord_add_guild_member_role(client, member->guild_id, member->user->id, server_config.access_role_id, NULL, NULL);
    // DISCORD_MESSAGE_GUILD_MEMBER_JOIN
}

int main(void) {

    enum {
        SERVER_DCRAFTBG_DEV,
        SERVER_DIEGO_ISLAND,
        SERVER_COUNT
    };
    const uint64_t servers_invited_in[SERVER_COUNT] = {
        970597463129997362,
        1441838850359169202,
    };
    ServerConfig configs[SERVER_COUNT] = {
        [SERVER_DCRAFTBG_DEV] = {
            .access_role_id  = 996104273463095336,
            .welcome_channel = 983274821003640872,
            .gulag_id        = 1443706033460740287,
            .gulag_role_id   = 1443705992906018937,
            .admin_role_id   = 1011248343382491187,
        },
        [SERVER_DIEGO_ISLAND] = {
            .access_role_id  = 1441839759776288818,
            .welcome_channel = 1441838851860726019,
            .gulag_id        = 1443642960942993589,
            .gulag_role_id   = 1443642846661050429,
            .admin_role_id   = 1441839342283788298,
        },
    };
    if(DbContext_init(&db, servers_invited_in, ARRAY_LEN(servers_invited_in))) return 1;
    for(size_t i = 0; i < ARRAY_LEN(configs); ++i) {
        if(DbContext_set_config(db, servers_invited_in[i], &configs[i])) return 1;
    }
    srand(time(NULL));
    struct discord *client = discord_init(BOT_TOKEN);
    discord_add_intents(client, DISCORD_GATEWAY_MESSAGE_CONTENT);
    discord_set_on_ready(client, &on_ready);
    discord_set_on_message_create(client, &on_message);
    discord_set_on_guild_member_add(client, &on_join);
    discord_run(client);
    DbContext_free(db);
}
