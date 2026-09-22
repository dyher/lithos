#ifndef HTTP_CLIENT_BRIDGE_H
#define HTTP_CLIENT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

int bridge_start_request(const char *url, const char *method, const char *body);
// Returns 1 if ready, 0 if not ready. If ready, out_body is malloc'd and must be freed.
int bridge_poll_result(int id, int *out_status, char **out_body);

#ifdef __cplusplus
}
#endif

#endif
