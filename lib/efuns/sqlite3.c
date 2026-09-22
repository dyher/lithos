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

// string sqlite3_query(int db_handle, string sql)
// 返回格式: "header1\theader2\nval1\tval2"
void f_sqlite3_query(void) {
    sqlite3 *db = (sqlite3*)(intptr_t)(sp-1)->u.number;
    const char *sql = SVALUE_STRPTR(sp);
    char **results = NULL;
    int rows, cols;
    char *err_msg = 0;
    char buffer[8192]; // 假設結果不會太大
    int offset = 0;

    if (sqlite3_get_table(db, sql, &results, &rows, &cols, &err_msg) != SQLITE_OK) {
        pop_n_elems(2);
        push_malloced_string(string_copy("ERROR", "f_sqlite3_query"));
        return;
    }

    buffer[0] = '\0';
    // 1. 寫入表頭
    for (int i = 0; i < cols; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s%s", results[i], (i < cols - 1) ? "\t" : "");
    }
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");

    // 2. 寫入數據行
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = (r + 1) * cols + c;
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s%s", results[idx] ? results[idx] : "NULL", (c < cols - 1) ? "\t" : "");
        }
        if (r < rows - 1) offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");
    }
    
    sqlite3_free_table(results);
    
    pop_n_elems(2);
    push_malloced_string(string_copy(buffer, "f_sqlite3_query"));
}

void f_sqlite3_close(void) {
    sqlite3 *db = (sqlite3*)(intptr_t)sp->u.number;
    if (db) sqlite3_close(db);
    pop_stack();
    push_number(0);
}
