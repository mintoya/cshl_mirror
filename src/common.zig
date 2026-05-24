pub const itemType = struct {
    alig: ?u16 = null,
    size: ?usize = null,
    typ: union(enum) {
        // type of a type
        typ: void,
        // opaque later
        // unknown: *void,

        // basic numbers
        inum: u16,
        unum: u16,
        // float
        // fnum: enum { float, double, longdouble },
        // pointer
        ptr: *itemType,
        // array, not slice
        arr: struct { type: *itemType, len: usize },
        // struct or union
        strc: []struct { type: *itemType, offset: usize },
        // function (not function pointer)
        func: struct { *itemType, []*itemType },
    },
    pub fn sizeof(self: @This()) usize {
        return self.size orelse switch (self.typ) {
            .typ => 0,
            .func => 0,
            .inum => |in| (in + 7) / 8,
            .unum => |in| (in + 7) / 8,
            .ptr => (itemType{ .typ = .{ .unum = 64 } }).sizeof(),
            .arr => |in| in.type.sizeof() * in.len,
            .strc => |in| r: {
                var max: usize = 0;
                for (in) |t| {
                    const size = t.type.sizeof() + t.offset;
                    max = if (size > max) size else max;
                }
                const alig = self.alignof();
                if (alig != 0)
                    max = if (max % alig != 0) max + (alig - (max % alig)) else max;
                break :r max;
            },
        };
    }
    pub fn alignof(self: @This()) usize {
        return self.alig orelse switch (self.typ) {
            .typ => 0,
            .func => 0,
            .inum => |in| (in + 7) / 8,
            .unum => |in| (in + 7) / 8,
            .ptr => (itemType{ .typ = .{ .unum = 64 } }).alignof(),
            .arr => |in| in.type.alignof(),
            .strc => |in| r: {
                var max: usize = 0;
                for (in) |t| {
                    const alig = t.type.alignof();
                    max = if (alig > max) alig else max;
                }
                break :r max;
            },
        };
    }
};

pub fn equalAny(a: anytype, check: []@TypeOf(a)) bool {
    inline for (check) |c| if (c == a) return true;
    return false;
}
pub const token = enum {
    kw_else, // else
    kw_enum, // enum
    kw_for, // for
    kw_if, // if
    kw_in, // in
    kw_return, // return
    kw_struct, // struct
    kw_to, // to
    kw_union, // union
    kw_up, // up
    kw_while, // while

    equalequal, // ==
    equals, // =
    greaterequal, // >=
    lessequal, // <=
    minusequal, // -=
    notequal, // !=
    andequal, // and=
    orequal, // or=
    pipe, // |>
    pipeequal, // |>=
    plusequal, // +=

    // literal operators
    and_, // and
    band, // band
    bnot, // bnot
    bor, // bor
    bxor, // bxor
    not, // not
    or_, // or
    xor, // xor

    // single char
    asterisk, // *
    bar, // |
    dot, // .
    greater, // >
    less, // <
    minus, // -
    plus, // +

    bitnumber, // 0b[0-1]+
    floatnumber, // [0-1]+.[0-1]+
    hexnumber, // 0x[0-1]+
    number, // [0-1]+
    string, // "..."
    identifier, //  anything else

    leftattr, // [[
    rightattr, // ]]
    leftbrace, // [
    rightbrace, // ]
    leftbracket, // {
    rightbracket, // }
    leftparen, // (
    rightparen, // )
    // single char & punctuation
    ampersand, // &
    bslash, // \
    colon, // :
    comma, // ,
    dollar, // $
    hash, // #
    quote, // '
    semi, // ;
    slash, // /
    // pub fn isKeyword(self: @This()) bool {
    //     return switch (self) {
    //         .kw_else,
    //         .kw_enum,
    //         .kw_for,
    //         .kw_if,
    //         .kw_in,
    //         .kw_return,
    //         .kw_struct,
    //         .kw_to,
    //         .kw_union,
    //         .kw_up,
    //         .kw_while,
    //         => true,
    //         else => false,
    //     };
    // }
};

pub const Token = struct {
    tag: token,
    loc: struct { col: usize, line: usize },
    text: []const u8,
};
