// lib/efuns/http_client.c
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "src/std.h"
#include "src/interpret.h"
#include "src/stralloc.h"
#include "lib/lpc/array.h"
#include "http_client_bridge.h"

void f_start_http_request(void) {
    const char *body_str = SVALUE_STRPTR(sp);
    const char *method_str = SVALUE_STRPTR(sp-1);
    const char *url_str = SVALUE_STRPTR(sp-2);
    
    int id = bridge_start_request(url_str, method_str, body_str);
    
    pop_n_elems(3);
    push_number(id);
}

void f_poll_http_result(void) {
    int id = (int)sp->u.number;
    int status;
    char *body;
    
    if (bridge_poll_result(id, &status, &body)) {
        pop_stack();
        
        array_t *arr = allocate_array(2);
        arr->item[0].type = T_NUMBER;
        arr->item[0].u.number = status;
        
        arr->item[1].type = T_STRING;
        arr->item[1].u.malloc_string = string_copy(body, "f_poll_http_result");
        
        free(body);
        push_refed_array(arr);
    } else {
        pop_stack();
        push_number(0);
    }
}
