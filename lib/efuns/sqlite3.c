// lib/efuns/sqlite3.c - Native Neolith Efun Style

// 1. 【關鍵】必須先引入 CMake 生成的 config.h (解決 NO_RETURN 未定義)
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// 2. 標準 C 庫 (解決 USHRT_MAX, intptr_t)
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 3. 引入 SQLite3 (在 LPC 巨集污染之前，讓它乾淨地載入系統標頭檔)
#include <sqlite3.h>

// 4. 引入 Neolith 核心 (使用相對於根目錄的路徑，這會定義 string, error 等巨集)
#include "src/std.h"
#include "src/interpret.h"
#include "src/stralloc.h"
#include "lib/lpc/array.h"
#include "rc/rc.h"

// LPC: int sqlite3_open(string path);
void f_sqlite3_open(void) {
    const char *path = SVALUE_STRPTR(sp);
    sqlite3 *db = NULL;
    
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        const char *err = sqlite3_errmsg(db);
        sqlite3_close(db);
        error("sqlite3_open failed: %s\n", err);
        return;
    }
    
    pop_stack(); 
    push_number((long)(intptr_t)db); 
}

// LPC: int sqlite3_exec(int db_handle, string sql);
void f_sqlite3_exec(void) {
    const char *sql = SVALUE_STRPTR(sp);
    sqlite3 *db = (sqlite3*)(intptr_t)(sp-1)->u.number;
    
    int is_select = 0;
    const char *p = sql;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "SELECT", 6) == 0) is_select = 1;
    
    if (is_select) {
        char **results = NULL;
        int rows, cols;
        char *err_msg = 0;
        
        if (sqlite3_get_table(db, sql, &results, &rows, &cols, &err_msg) != SQLITE_OK) {
            pop_n_elems(2);
            push_malloced_string(string_copy("ERROR", "f_sqlite3_exec"));
            if (err_msg) sqlite3_free(err_msg);
            return;
        }
        
        char buffer[16384];
        int offset = 0;
        buffer[0] = '\0';
        
        for (int i = 0; i < cols && offset < 16000; i++) {
            offset += snprintf(buffer + offset, 16384 - offset, "%s%s", results[i], (i < cols - 1) ? "\t" : "");
        }
        offset += snprintf(buffer + offset, 16384 - offset, "\n");
        
        for (int r = 0; r < rows && offset < 16000; r++) {
            for (int c = 0; c < cols && offset < 16000; c++) {
                int idx = (r + 1) * cols + c;
                offset += snprintf(buffer + offset, 16384 - offset, "%s%s", results[idx] ? results[idx] : "NULL", (c < cols - 1) ? "\t" : "");
            }
            if (r < rows - 1) offset += snprintf(buffer + offset, 16384 - offset, "\n");
        }
        
        sqlite3_free_table(results);
        pop_n_elems(2);
        push_malloced_string(string_copy(buffer, "f_sqlite3_exec"));
    } else {
        char *err_msg = 0;
        int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
        if (err_msg) sqlite3_free(err_msg);
        
        pop_n_elems(2);
        push_number(rc == SQLITE_OK ? 1 : 0);
    }
}

void f_sqlite3_close(void) {
    sqlite3 *db = (sqlite3*)(intptr_t)sp->u.number;
    if (db) sqlite3_close(db);
    pop_stack();
    push_number(0);
}
