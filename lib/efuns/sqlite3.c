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
    char *err_msg = 0;
    
    if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "sqlite3_exec failed: %s\n", err_msg ? err_msg : "Unknown error");
        if (err_msg) sqlite3_free(err_msg);
        error(tmp); // error() 會拋出 LPC 異常
        return;
    }
    
    pop_n_elems(2);
    push_number(1); 
}

// LPC: void sqlite3_close(int db_handle);
void f_sqlite3_close(void) {
    sqlite3 *db = (sqlite3*)(intptr_t)sp->u.number;
    if (db) sqlite3_close(db);
    pop_stack();
    push_number(0);
}
