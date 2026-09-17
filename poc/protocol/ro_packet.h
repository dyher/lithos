/* ro_packet.h — RO client protocol packet structures
 * Aligned with rAthena's actual wire format.
 * RO protocol is little-endian; ARM64 is little-endian, so no swap needed.
 */
#ifndef RO_PACKET_H
#define RO_PACKET_H

#include <stdint.h>

#pragma pack(push, 1)   /* critical: no padding, match wire format exactly */

/* ---- Login request: client -> login server (0x0064, PACKET_CA_LOGIN) ---- */
typedef struct {
    int16_t  packet_type;    /* 0x0064 */
    uint32_t version;
    char     username[24];
    char     password[24];
    uint8_t  client_type;
} ca_login_t;                /* 2+4+24+24+1 = 55 bytes */

/* ---- Server entry inside login-accept packet ---- */
typedef struct {
    uint32_t ip;
    uint16_t port;
    char     name[20];
    uint16_t users;
    uint16_t type;
    uint16_t is_new;
} server_info_t;             /* 4+2+20+2+2+2 = 32 bytes */

/* ---- Login accept: login server -> client (0x0069, PACKET_AC_ACCEPT_LOGIN) ---- */
typedef struct {
    int16_t  packet_type;    /* 0x0069 */
    int16_t  packet_len;
    uint32_t auth_code;
    uint32_t aid;            /* account id */
    uint32_t login_id1;
    uint32_t login_id2;
    uint32_t sex;
    /* server_info_t list follows (variable length) */
} ac_accept_login_t;         /* fixed header = 24 bytes */

#pragma pack(pop)

#define CA_LOGIN_ID        0x0064
#define AC_ACCEPT_LOGIN_ID 0x0069
#define CA_LOGIN_SIZE      ((int)sizeof(ca_login_t))
#define AC_HEADER_SIZE     ((int)sizeof(ac_accept_login_t))
#define SERVER_INFO_SIZE   ((int)sizeof(server_info_t))

/* Parse a login request from a raw byte buffer. Returns 0 on success. */
int ro_parse_login(const uint8_t *buf, int len, ca_login_t *out);

/* Build a login-accept response with one char server. Returns byte length. */
int ro_build_login_accept(uint8_t *buf, int bufsize,
                          uint32_t aid, const char *server_name,
                          uint32_t server_ip, uint16_t server_port);

#endif
