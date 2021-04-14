#include <runtime.h>


static char *hex_digit="0123456789abcdef";
void print_byte(buffer s, u8 f)
{
    push_u8(s, hex_digit[f >> 4]);
    push_u8(s, hex_digit[f & 15]);
}

void print_hex_buffer(buffer s, buffer b)
{
    int len = buffer_length(b);
    int wlen = 4;
    int rowlen = wlen * 4;
    boolean first = true;

    for (int i = 0 ; i<len ; i+= 1) {
        if (!(i % rowlen)) {
            if (!first) push_u8(s, '\n');
            first = false;
            print_byte(s, i>>24);
            print_byte(s, i>>16);
            print_byte(s, i>>8);
            print_byte(s, i);
            push_u8(s, ':');
        }
        if (!(i % wlen)) push_u8 (s, ' ');
        print_byte(s, *(u8 *)buffer_ref(b, i));
        push_u8(s, ' ');
    }
    // better handling of empty buffer
    push_u8(s, '\n');
}

void print_uuid(buffer b, u8 *uuid)
{
    /* UUID format: 00112233-4455-6677-8899-aabbccddeeff */
    for (int i = 0; i < 4; i++)
        bprintf(b, "%02x", uuid[i]);
    bprintf(b, "-%02x%02x-%02x%02x-%02x%02x-", uuid[4], uuid[5], uuid[6],
            uuid[7], uuid[8], uuid[9]);
    for (int i = 10; i < 16; i++)
        bprintf(b, "%02x", uuid[i]);
}
KLIB_EXPORT(print_uuid);

/* just a little tool for debugging */
void print_csum_buffer(buffer s, buffer b)
{
    u64 csum = 0;
    for (int i = 0; i < buffer_length(b); i++)
        csum += *(u8*)buffer_ref(b, i);
    bprintf(s, "%lx", csum);
}

closure_function(1, 2, boolean, _sort_handler,
                 vector, pairs,
                 symbol, s, value, v)
{
    vector_push(bound(pairs), s);
    vector_push(bound(pairs), v);
    return true;
}

static boolean _symptr_compare(void *a, void *b)
{
    symbol s1 = *(symbol*)a;
    symbol s2 = *(symbol*)b;
    return buffer_lt(symbol_string(s2), symbol_string(s1));
}

static void print_value_internal(buffer dest, value v, table visited, s32 indent, s32 depth);

#define TUPLE_INDENT_SPACES 2
static void print_tuple_internal(buffer b, tuple t, table visited, s32 indent, s32 depth)
{
    /* This is a little heavy, but we don't have a sorted iterate. */
    pqueue pq = allocate_pqueue(transient, _symptr_compare);
    assert(pq != INVALID_ADDRESS);
    vector v = allocate_vector(transient, 16);
    assert(v != INVALID_ADDRESS);
    iterate(t, stack_closure(_sort_handler, v));

    for (int i = 0; i < vector_length(v); i += 2) {
        void *p = buffer_ref(v, i * sizeof(void *));
        pqueue_insert(pq, p);
    }

    bprintf(b, "(");
    if (indent >= 0)
        indent++;
    void **p;
    boolean sub = false;
    while ((p = pqueue_pop(pq)) != INVALID_ADDRESS) {
        symbol s = p[0];
        value v = p[1];
        if (sub) {
            if (indent >= 0)
                bprintf(b, "\n%n", indent);
            else
                bprintf(b, " ");
        } else {
            sub = true;
        }
        bytes start = buffer_length(b);
        bprintf(b, "%b:", symbol_string(s));
        s32 next_indent = indent >= 0 ? indent + buffer_length(b) - start : indent;
        print_value_internal(b, v, visited, next_indent, depth);
    }
    bprintf(b, ")");
    deallocate_vector(v);
    deallocate_pqueue(pq);
}

/* XXX Marking root as visited breaks with wrapped root...eval method? */

static void print_value_internal(buffer dest, value v, table visited, s32 indent, s32 depth)
{
    if (is_tuple(v)) {
        if (!visited) {
            visited = allocate_table(transient, identity_key, pointer_equal);
            assert(visited != INVALID_ADDRESS);
        }

        if (table_find(visited, v)) {
            bprintf(dest, "<visited>");
        } else {
            table_set(visited, v, (void *)1);
            if (depth > 0)
                print_tuple_internal(dest, v, visited, indent, depth - 1);
            else
                bprintf(dest, "<pruned>");
        }
    } else if (is_symbol(v))
        bprintf(dest, "%b", symbol_string((symbol)v));
    else {
        // XXX string vs binary
        buffer b = (buffer)v;
        if (buffer_length(b) > 20)
            bprintf(dest, "{buffer %d}", buffer_length(b));
        else
            bprintf(dest, "%b", b);
    }
}

void print_value(buffer dest, value v, tuple attrs)
{
    u64 indent = (s32)-1;
    u64 depth = 0;

    if (attrs) {
        get_u64(attrs, sym(indent), &indent);
        get_u64(attrs, sym(depth), &depth);
    }
    print_value_internal(dest, v, 0, indent, depth == 0 ? S32_MAX : depth);
}

static void format_value(buffer dest, struct formatter_state *s, vlist *v)
{
    value x = varg(*v, value);
    if (!x) {
        bprintf(dest, "(none)");
        return;
    }

    print_value(dest, x, 0);
}

static void format_value_with_attributes(buffer dest, struct formatter_state *s, vlist *v)
{
    value x = varg(*v, value);
    if (!x) {
        bprintf(dest, "(none)");
        return;
    }

    value a = varg(*v, tuple);
    assert(!a || is_tuple(a));
    print_value(dest, x, a);
}

static void format_hex_buffer(buffer dest, struct formatter_state *s, vlist *a)
{
    buffer b = varg(*a, buffer);
    print_hex_buffer(dest, b);
}

static void format_csum_buffer(buffer dest, struct formatter_state *s, vlist *a)
{
    buffer b = varg(*a, buffer);
    print_csum_buffer(dest, b);
}

static void format_timestamp(buffer dest, struct formatter_state *s, vlist *a)
{
    timestamp t = varg(*a, timestamp);
    print_timestamp(dest, t);
}

static void format_range(buffer dest, struct formatter_state *s, vlist *a)
{
    range r = varg(*a, range);
    bprintf(dest, "[0x%lx 0x%lx)", r.start, r.end);
}

static void format_closure(buffer dest, struct formatter_state *s, vlist *a)
{
    // xxx - we can probably do better here?
    void **k = varg(*a, void **);
    struct _closure_common *c = k[1];
    bprintf(dest, "%s", &c->name);
}

void init_extra_prints(void)
{
    register_format('v', format_value, 0);
    register_format('V', format_value_with_attributes, 0);
    register_format('X', format_hex_buffer, 0);
    register_format('T', format_timestamp, 0);
    register_format('R', format_range, 0);
    register_format('C', format_csum_buffer, 0);
    register_format('F', format_closure, 0);
}
