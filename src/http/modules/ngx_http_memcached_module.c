
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#ifndef NGX_UINT32_LEN
#define NGX_UINT32_LEN (NGX_INT32_LEN - 1)
#endif

typedef struct {
    ngx_http_upstream_conf_t   upstream;
    ngx_int_t                  index;
    ngx_flag_t                 allow_put;
    ngx_flag_t                 stats;
    ngx_flag_t                 flush;
    ngx_uint_t                 method_filter;
} ngx_http_memcached_loc_conf_t;


typedef struct {
    size_t                     rest;
    ngx_http_request_t        *request;
    ngx_str_t                  key;
    u_char                    *end;
    size_t                     end_len;
} ngx_http_memcached_ctx_t;


static ngx_int_t ngx_http_memcached_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_create_request_set(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_create_request_flush(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_create_request_stats(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_process_header_set(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_process_header_flush(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_process_header_stats(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_filter_init(void *data);
static ngx_int_t ngx_http_memcached_filter(void *data, ssize_t bytes);
static void ngx_http_memcached_abort_request(ngx_http_request_t *r);
static void ngx_http_memcached_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static void *ngx_http_memcached_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_memcached_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_memcached_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_conf_bitmask_t  ngx_http_memcached_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("not_found"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_memcached_commands[] = {

    { ngx_string("memcached_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_memcached_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("memcached_allow_put"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, allow_put),
      NULL },

    { ngx_string("memcached_stats"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, stats),
      NULL },

    { ngx_string("memcached_flush"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, flush),
      NULL },
    
    { ngx_string("memcached_bind"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_bind_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.local),
      NULL },

    { ngx_string("memcached_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("memcached_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("memcached_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("memcached_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("memcached_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.next_upstream),
      &ngx_http_memcached_next_upstream_masks },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_memcached_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_memcached_create_loc_conf,    /* create location configration */
    ngx_http_memcached_merge_loc_conf      /* merge location configration */
};


ngx_module_t  ngx_http_memcached_module = {
    NGX_MODULE_V1,
    &ngx_http_memcached_module_ctx,        /* module context */
    ngx_http_memcached_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_str_t  ngx_http_memcached_key = ngx_string("memcached_key");


#define NGX_HTTP_MEMCACHED_END   (sizeof(ngx_http_memcached_end) - 1)
static u_char  ngx_http_memcached_end[] = CRLF "END" CRLF;

#define NGX_HTTP_MEMCACHED_CRLF   (sizeof(ngx_http_memcached_crlf) - 1)
static u_char  ngx_http_memcached_crlf[] = CRLF;


static ngx_int_t
ngx_http_memcached_handler(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_http_upstream_t            *u;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_memcached_loc_conf_t  *mlcf;
    ngx_flag_t                      read_body;

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    ngx_str_set(&u->schema, "memcached://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_memcached_module;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    if (!(r->method & mlcf->method_filter)) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    u->conf = &mlcf->upstream;

    u->reinit_request = ngx_http_memcached_reinit_request;
    u->abort_request = ngx_http_memcached_abort_request;
    u->finalize_request = ngx_http_memcached_finalize_request;

    ctx = ngx_palloc(r->pool, sizeof(ngx_http_memcached_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->request = r;

    ngx_http_set_ctx(r, ctx, ngx_http_memcached_module);

    u->input_filter_init = ngx_http_memcached_filter_init;
    u->input_filter = ngx_http_memcached_filter;
    u->input_filter_ctx = ctx;

    read_body = 0;
    
    if (mlcf->flush) {
      ctx->rest = ctx->end_len = NGX_HTTP_MEMCACHED_CRLF;
      ctx->end = ngx_http_memcached_crlf;
      u->create_request = ngx_http_memcached_create_request_flush;
      u->process_header = ngx_http_memcached_process_header_flush;
    }
    else if (mlcf->stats) {
      ctx->rest = ctx->end_len = NGX_HTTP_MEMCACHED_END;
      ctx->end = ngx_http_memcached_end;
      u->create_request = ngx_http_memcached_create_request_stats;
      u->process_header = ngx_http_memcached_process_header_stats;
    }
    else if(r->method & (NGX_HTTP_PUT)) {
      read_body = 1;
      ctx->rest = ctx->end_len = NGX_HTTP_MEMCACHED_CRLF;
      ctx->end = ngx_http_memcached_crlf;
      u->create_request = ngx_http_memcached_create_request_set;
      u->process_header = ngx_http_memcached_process_header_set;      
     
    }
    else {
      ctx->rest = ctx->end_len = NGX_HTTP_MEMCACHED_END;
      ctx->end = ngx_http_memcached_end;
      u->create_request = ngx_http_memcached_create_request;
      u->process_header = ngx_http_memcached_process_header;
    }
    
    if (read_body) {
      rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);
      if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
      }
    }
    else {
      r->main->count++;
      rc = ngx_http_discard_request_body(r);
      if (rc != NGX_OK) {
        return rc;
      }
      ngx_http_upstream_init(r);
    }
 
    return NGX_DONE;
}

static ngx_int_t
ngx_http_memcached_create_request(ngx_http_request_t *r)
{
    size_t                          len;
    uintptr_t                       escape;
    ngx_buf_t                      *b;
    ngx_chain_t                    *cl;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_variable_value_t      *vv;
    ngx_http_memcached_loc_conf_t  *mlcf;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    vv = ngx_http_get_indexed_variable(r, mlcf->index);

    if (vv == NULL || vv->not_found || vv->len == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "the \"$memcached_key\" variable is not set");
        return NGX_ERROR;
    }

    escape = 2 * ngx_escape_uri(NULL, vv->data, vv->len, NGX_ESCAPE_MEMCACHED);

    len = sizeof("get ") - 1 + vv->len + escape + sizeof(CRLF) - 1;

    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;

    r->upstream->request_bufs = cl;

    *b->last++ = 'g'; *b->last++ = 'e'; *b->last++ = 't'; *b->last++ = ' ';

    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);

    ctx->key.data = b->last;

    if (escape == 0) {
        b->last = ngx_copy(b->last, vv->data, vv->len);

    } else {
        b->last = (u_char *) ngx_escape_uri(b->last, vv->data, vv->len,
                                            NGX_ESCAPE_MEMCACHED);
    }

    ctx->key.len = b->last - ctx->key.data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http memcached request: \"%V\"", &ctx->key);

    *b->last++ = CR; *b->last++ = LF;

    return NGX_OK;
}

static ngx_int_t
ngx_http_memcached_create_request_set(ngx_http_request_t *r)
{
    size_t                          len;
    uintptr_t                       escape, bytes_len;
    off_t                           bytes;
    ngx_buf_t                      *b;
    ngx_chain_t                    *cl, *in;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_variable_value_t      *vv;
    ngx_http_memcached_loc_conf_t  *mlcf;
    u_char                          bytes_buf[NGX_UINT32_LEN];

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    vv = ngx_http_get_indexed_variable(r, mlcf->index);

    if (vv == NULL || vv->not_found || vv->len == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "the \"$memcached_key\" variable is not set");
        return NGX_ERROR;
    }

    escape = 2 * ngx_escape_uri(NULL, vv->data, vv->len, NGX_ESCAPE_MEMCACHED);
    
    bytes = 0;
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        bytes += ngx_buf_size(cl->buf);
    }
    
    if (bytes != r->headers_in.content_length_n) {
      ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "memcached put : wrong content length size, headers %d, found %d", r->headers_in.content_length_n, bytes);
      return NGX_ERROR;
    }
    
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                      "memcached put : size %d", bytes);
    
    bytes_len = ngx_snprintf(bytes_buf, sizeof(bytes_buf), "%O", bytes) - bytes_buf;
                       
    len = sizeof("set ") - 1 + vv->len + escape + sizeof(" 0 0 ") - 1 + bytes_len + sizeof(CRLF) - 1;
    
    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
      return NGX_ERROR;
    }
    
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
      return NGX_ERROR;
    }
    
    cl->buf = b;
    cl->next = NULL;
    r->upstream->request_bufs = cl;
    
    *b->last++ = 's'; *b->last++ = 'e'; *b->last++ = 't'; *b->last++ = ' ';
    
    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);
    
    ctx->key.data = b->last;

    if (escape == 0) {
        b->last = ngx_copy(b->last, vv->data, vv->len);

    } else {
        b->last = (u_char *) ngx_escape_uri(b->last, vv->data, vv->len,
                                            NGX_ESCAPE_MEMCACHED);
    }

    ctx->key.len = b->last - ctx->key.data;

    *b->last++ = ' ';
    *b->last++ = '0';
    *b->last++ = ' ';
    *b->last++ = '0';
    *b->last++ = ' ';
    
    b->last = ngx_copy(b->last, bytes_buf, bytes_len);
    
    *b->last++ = CR; *b->last++ = LF;
    
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                        "memcached put : on key %V, size %d", &ctx->key, bytes);
                        
    in = r->request_body->bufs;

    while (in) {
      cl->next = ngx_alloc_chain_link(r->pool);
      cl = cl->next;
      if (cl == NULL) {
        return NGX_ERROR;
      }
    
      cl->buf = ngx_calloc_buf(r->pool);
      if (cl->buf == NULL) {
        return NGX_ERROR;
      }
    
      cl->buf->memory = 1;
      *cl->buf = *in->buf;
    
      in = in->next;
    }
    
    b = ngx_calloc_buf(r->pool);
    
    if (b == NULL) {
        return NGX_ERROR;
    }
    
    b->start = b->pos = (u_char *) CRLF;
    b->last  = b->end = b->start + sizeof(CRLF) - 1;
    
    b->memory = 1;
    
    cl->next = ngx_alloc_chain_link(r->pool);
    cl = cl->next;
    if (cl == NULL) {
        return NGX_ERROR;
    }
    
    cl->buf = b;
    cl->next = NULL;
    
    return NGX_OK;
}

static ngx_int_t
ngx_http_memcached_create_request_fixed_str(ngx_http_request_t *r, char * cmd, char * str, u_int str_len)
{
    u_int                           i;
    ngx_buf_t                      *b;
    ngx_chain_t                    *cl;
    ngx_http_memcached_ctx_t       *ctx;
    
    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);
    
    ctx->key.data = (u_char *) str;
    ctx->key.len = str_len;
    
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "memcached %s requested", cmd);

    b = ngx_create_temp_buf(r->pool, str_len + 2);
    if (b == NULL) {
      return NGX_ERROR;
    }
    
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
      return NGX_ERROR;
    }
    
    cl->buf = b;
    cl->next = NULL;
    
    r->upstream->request_bufs = cl;
    
    for(i = 0; i < str_len; i ++) {
      *b->last++ = str[i];
    }
    
    *b->last++ = CR; *b->last++ = LF;
    
    return NGX_OK;
}

static ngx_int_t
ngx_http_memcached_create_request_flush(ngx_http_request_t *r)
{
  return ngx_http_memcached_create_request_fixed_str(r, "flush", "flush_all", sizeof("flush_all") - 1);
}

static ngx_int_t
ngx_http_memcached_create_request_stats(ngx_http_request_t *r)
{
  return ngx_http_memcached_create_request_fixed_str(r, "stats", "stats", sizeof("stats") - 1);
}

static ngx_int_t
ngx_http_memcached_reinit_request(ngx_http_request_t *r)
{
    return NGX_OK;
}

static ngx_int_t
ngx_http_memcached_process_header(ngx_http_request_t *r)
{
    u_char                    *p, *len;
    ngx_str_t                  line;
    ngx_http_upstream_t       *u;
    ngx_http_memcached_ctx_t  *ctx;

    u = r->upstream;

    for (p = u->buffer.pos; p < u->buffer.last; p++) {
        if (*p == LF) {
            goto found;
        }
    }

    return NGX_AGAIN;

found:

    *p = '\0';

    line.len = p - u->buffer.pos - 1;
    line.data = u->buffer.pos;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "memcached response: \"%V\"", &line);

    p = u->buffer.pos;

    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);
    
    if (ngx_strncmp(p, "VALUE ", sizeof("VALUE ") - 1) == 0) {

        p += sizeof("VALUE ") - 1;

        if (ngx_strncmp(p, ctx->key.data, ctx->key.len) != 0) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "memcached sent invalid key in response \"%V\" "
                          "for key \"%V\"",
                          &line, &ctx->key);

            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        p += ctx->key.len;

        if (*p++ != ' ') {
            goto no_valid;
        }

        /* skip flags */

        while (*p) {
            if (*p++ == ' ') {
                goto length;
            }
        }

        goto no_valid;

    length:

        len = p;

        while (*p && *p++ != CR) { /* void */ }

        u->headers_in.content_length_n = ngx_atoof(len, p - len - 1);
        if (u->headers_in.content_length_n == -1) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "memcached sent invalid length in response \"%V\" "
                          "for key \"%V\"",
                          &line, &ctx->key);
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        p ++;
        
#define EXTRACT_HEADERS "EXTRACT_HEADERS\r\n"

        if (ngx_strncmp(p, EXTRACT_HEADERS, sizeof(EXTRACT_HEADERS) - 1) == 0) {
          u_char *delim, *name, *value;
          int name_len, value_len;
          
          ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                         "extracting headers from memcached value");
          
          p += sizeof(EXTRACT_HEADERS) - 1;
          u->headers_in.content_length_n -= sizeof(EXTRACT_HEADERS) - 1;

          while (1) {
            if (u->headers_in.content_length_n < 2) {
              ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "unable to read http headers in memcached value : end of headers not found");
              return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
            if (ngx_strncmp(p, "\r\n", 2) == 0) {
              p += 2;
              u->headers_in.content_length_n -= 2;
              break;
            }
              
            delim = (u_char *) ngx_strstr(p, ": ");
            if (delim == NULL) {
              ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "unable to read http headers in memcached value");
              return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
            name_len = delim - p;
            name = (u_char *) ngx_palloc(r->pool, name_len + 1);
            if (name == NULL) {
              return NGX_ERROR;
            }
            ngx_memcpy(name, p, name_len);
            name[name_len] = 0;
            p = delim + 2;
            u->headers_in.content_length_n -= name_len + 2;
            delim = (u_char *) ngx_strstr(p, "\r\n");
            if (delim == NULL) {
              ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "unable to read http headers in memcached value");
              return NGX_HTTP_UPSTREAM_INVALID_HEADER;
            }
            value_len = delim - p;
            value = (u_char *) ngx_palloc(r->pool, value_len + 1);
            if (value == NULL) {
              return NGX_ERROR;
            }
            ngx_memcpy(value, p, value_len);
            value[value_len] = 0;
            p = delim + 2;
            u->headers_in.content_length_n -= value_len + 2;
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "http header read in memcached  : %s: %s ", name, value);
              
            if (ngx_strcmp(name, "Content-Type") == 0) {
              r->headers_out.content_type.data = value;
              r->headers_out.content_type.len = value_len;
              r->headers_out.content_type_len = value_len;
              r->headers_out.content_type_lowcase = NULL;
            }
            else {
              ngx_table_elt_t *h = ngx_list_push(&r->headers_out.headers);
              if (h == NULL) {
                return NGX_ERROR;
              }
              
              h->key.data = name;
              h->key.len = name_len;
              
              h->value.data = value;
              h->value.len = value_len;
              h->hash = 1;
              
              if (ngx_strcmp(name, "Server") == 0) {
                r->headers_out.server = h;
              }
              if (ngx_strcmp(name, "Date") == 0) {
                r->headers_out.date = h;
              }
              if (ngx_strcmp(name, "Content-Length") == 0) {
                r->headers_out.content_length = h;
              }
              if (ngx_strcmp(name, "Content-Encoding") == 0) {
                r->headers_out.content_encoding = h;
              }
              if (ngx_strcmp(name, "Location") == 0) {
                r->headers_out.location = h;
              }
              if (ngx_strcmp(name, "Refresh") == 0) {
                r->headers_out.refresh = h;
              }
              if (ngx_strcmp(name, "Last-Modified") == 0) {
                r->headers_out.last_modified = h;
              }
              if (ngx_strcmp(name, "Content-Range") == 0) {
                r->headers_out.content_range = h;
              }
              if (ngx_strcmp(name, "Accept-Ranges") == 0) {
                r->headers_out.accept_ranges = h;
              }
              if (ngx_strcmp(name, "WWW-Authenticate") == 0) {
                r->headers_out.www_authenticate = h;
              }
              if (ngx_strcmp(name, "Expires") == 0) {
                r->headers_out.expires = h;
              }
              if (ngx_strcmp(name, "Etag") == 0) {
                r->headers_out.etag = h;
              }
            }
          }
        }

        u->headers_in.status_n = 200;
        u->state->status = 200;
        u->buffer.pos = p;

        return NGX_OK;
    }

    if (ngx_strcmp(p, "END\x0d") == 0) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "key: \"%V\" was not found by memcached", &ctx->key);

        u->headers_in.status_n = 404;
        u->state->status = 404;
        u->keepalive = 1;

        return NGX_OK;
    }

no_valid:

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "memcached sent invalid response: \"%V\"", &line);

    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
}

static ngx_int_t
ngx_http_memcached_process_header_fixed_string(ngx_http_request_t *r, char * cmd, char * str, u_int str_len)
{
    u_char                    *p;
    ngx_str_t                  line;
    ngx_http_upstream_t       *u;
    ngx_http_memcached_ctx_t  *ctx;
    
    u = r->upstream;

    for (p = u->buffer.pos; p < u->buffer.last; p++) {
        if (*p == LF) {
            goto found;
        }
    }

    return NGX_AGAIN;

found:

    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);

    line.len = p - u->buffer.pos - 1;
    line.data = u->buffer.pos;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                  "memcached response: \"%V\" for key \"%V\"", &line, &ctx->key);

    p = u->buffer.pos;
    
    if (ngx_strncmp(p, str, str_len) != 0) {
      ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "memcached %s invalid response for key \"%V\"",
                    cmd, &ctx->key);
      return NGX_HTTP_UPSTREAM_INVALID_HEADER;
    }
    
    r->headers_out.content_type.data = (u_char *) "text/plain";
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type_len = sizeof("text/plain") - 1;
    r->headers_out.content_type_lowcase = NULL;
    
    u->headers_in.status_n = 200;
    u->state->status = 200;
    u->buffer.pos = p;
    u->headers_in.content_length_n = line.len;
    
    return NGX_OK;
}


static ngx_int_t
ngx_http_memcached_process_header_set(ngx_http_request_t *r)
{
  return ngx_http_memcached_process_header_fixed_string(r, "set", "STORED", sizeof("STORED") - 1);
}

static ngx_int_t
ngx_http_memcached_process_header_flush(ngx_http_request_t *r)
{
  ngx_int_t rc;
  rc = ngx_http_memcached_process_header_fixed_string(r, "flush", "OK", sizeof("OK") - 1);
  if (rc == NGX_OK) {
     ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "memcached flush OK");
  }
  return rc;
}

static ngx_int_t
ngx_http_memcached_process_header_stats(ngx_http_request_t *r)
{
    u_char                    *p, *last_p;
    ngx_str_t                  line;
    ngx_http_upstream_t       *u;
    
    u = r->upstream;

    for (p = u->buffer.pos; p < u->buffer.last; p++) {
        if (*p == LF) {
            goto found;
        }
    }

    return NGX_AGAIN;

found:

    u->headers_in.content_length_n = 0;
    
    p = last_p = u->buffer.pos;
    for (/* void */; p < u->buffer.last; p++) {
        if (*p == LF) {
          line.len = p - last_p - 1;
          line.data = last_p;

          ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                         "memcache stats line read : \"%V\", current response size : %d", &line, u->headers_in.content_length_n);
          
          if (ngx_strncmp(line.data, "END", sizeof("END") - 1) == 0) {
            u->headers_in.status_n = 200;
            u->state->status = 200;
            u->headers_in.content_length_n -= 2;
            
            r->headers_out.content_type.data = (u_char *) "text/plain";
            r->headers_out.content_type.len = sizeof("text/plain") - 1;
            r->headers_out.content_type_len = sizeof("text/plain") - 1;
            r->headers_out.content_type_lowcase = NULL;
            
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "memcache stats end reach, final response size : %d", u->headers_in.content_length_n);
            ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                          "memcached stats OK");
            
            return NGX_OK;
          }
          u->headers_in.content_length_n += line.len + 2;
          last_p = p + 1;
        }
    }
    
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "memcached stats invalid response");
    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
}

static ngx_int_t
ngx_http_memcached_filter_init(void *data)
{
    ngx_http_memcached_ctx_t  *ctx = data;

    ngx_http_upstream_t  *u;

    u = ctx->request->upstream;

    u->length += ctx->end_len;

    return NGX_OK;
}

static ngx_int_t
ngx_http_memcached_filter(void *data, ssize_t bytes)
{
    ngx_http_memcached_ctx_t  *ctx = data;

    u_char               *last;
    ngx_buf_t            *b;
    ngx_chain_t          *cl, **ll;
    ngx_http_upstream_t  *u;

    u = ctx->request->upstream;
    b = &u->buffer;
    
    if (u->length == (ssize_t) ctx->rest) {

        if (ngx_strncmp(b->last,
                   ctx->end + ctx->end_len - ctx->rest,
                   bytes)
            != 0)
        {
            ngx_log_error(NGX_LOG_ERR, ctx->request->connection->log, 0,
                          "memcached sent invalid trailer");

            u->length = 0;
            ctx->rest = 0;

            return NGX_OK;
        }

        u->length -= bytes;
        ctx->rest -= bytes;

        if (u->length == 0) {
            u->keepalive = 1;
        }

        return NGX_OK;
    }

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    cl = ngx_chain_get_free_buf(ctx->request->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf->flush = 1;
    cl->buf->memory = 1;

    *ll = cl;

    last = b->last;
    cl->buf->pos = last;
    b->last += bytes;
    cl->buf->last = b->last;
    cl->buf->tag = u->output.tag;

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, ctx->request->connection->log, 0,
                   "memcached filter bytes:%z size:%z length:%z rest:%z",
                   bytes, b->last - b->pos, u->length, ctx->rest);

    if (bytes <= (ssize_t) (u->length - ctx->end_len)) {
        u->length -= bytes;
        return NGX_OK;
    }

    last += u->length - ctx->end_len;

    if (ngx_strncmp(last, ctx->end, b->last - last) != 0) {
        ngx_log_error(NGX_LOG_ERR, ctx->request->connection->log, 0,
                      "memcached sent invalid trailer");

        b->last = last;
        cl->buf->last = last;
        u->length = 0;
        ctx->rest = 0;

        return NGX_OK;
    }

    ctx->rest -= b->last - last;
    b->last = last;
    cl->buf->last = last;
    u->length = ctx->rest;

    if (u->length == 0) {
        u->keepalive = 1;
    }
    
    return NGX_OK;
}


static void
ngx_http_memcached_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http memcached request");
    return;
}


static void
ngx_http_memcached_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http memcached request");
    return;
}


static void *
ngx_http_memcached_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_memcached_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_memcached_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     */

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;

    conf->allow_put = NGX_CONF_UNSET;
    conf->stats = NGX_CONF_UNSET;
    conf->flush = NGX_CONF_UNSET;

    conf->index = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_memcached_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_memcached_loc_conf_t *prev = parent;
    ngx_http_memcached_loc_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    if (conf->index == NGX_CONF_UNSET) {
        conf->index = prev->index;
    }

    if (conf->index == NGX_CONF_UNSET) {
        conf->index = prev->index;
    }
    
    if (conf->allow_put == NGX_CONF_UNSET) {
      conf->allow_put = 0;
    }

    if (conf->stats == NGX_CONF_UNSET) {
      conf->stats = 0;
    }

    if (conf->flush == NGX_CONF_UNSET) {
      conf->flush = 0;
    }
    
    if ((conf->flush && conf->stats) || (conf->flush && conf->allow_put) || (conf->stats && conf->allow_put)) {
      ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "memcached configuration : stats, flush and allow put are mutually exclusive");
      return NGX_CONF_ERROR;
    }
    
    if (conf->flush || conf->stats) {
      conf->method_filter = NGX_HTTP_GET;
    }
    else {
      conf->method_filter = conf->allow_put ? NGX_HTTP_GET|NGX_HTTP_HEAD|NGX_HTTP_PUT : NGX_HTTP_GET|NGX_HTTP_HEAD;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_memcached_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_memcached_loc_conf_t *mlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                  u;
    ngx_http_core_loc_conf_t  *clcf;

    if (mlcf->upstream.upstream) {
        return "is duplicate";
    }

    value = cf->args->elts;

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;

    mlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (mlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_memcached_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    mlcf->index = ngx_http_get_variable_index(cf, &ngx_http_memcached_key);

    if (mlcf->index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
