const std = @import("std");
const Allocator = std.mem.Allocator;
const File = std.Io.File;
const sList = @import("slist").SmallList;

pub const itemType = union(enum) {
    typ: void,
    snum: usize,
    unum: usize,
    // fnum: enum { float, double, longdouble },
    ptr: *itemType,
    arr: struct { type: *itemType, len: usize },
    struc: []struct { type: *itemType, offset: usize },
    func: []*itemType,
};
pub const attribute = union(enum) {
    value: struct {
        connected_lifetimes: ?*std.ArrayListUnmanaged(*void, 5),
    },
    function: struct {
        allocates: bool = false,
        is_extern: bool = false,
    },
};

//
// tokenizer
//

pub const token = enum {
    // single char
    leftbrace,
    rightbrace,
    leftparen,
    rightparen,
    leftbracket,
    rightbracket,
    colon,
    semi,
    comma,
    equals,
    dot,
    asterisk,
    plus,
    minus,
    bar,
    greater,
    less,
    dollar,
    quote,
    ampersand,
    hash,
    slash,
    bslash,

    // multi char
    equalequal,
    andequal,
    orequal,
    notequal,
    lessequal,
    greaterequal,
    plusequal,
    minusequal,
    pipe, // |>
    pipeequal, // |>=
    leftattr, // [[
    rightattr, // ]]
    // literal operators
    and_,
    band,
    or_,
    bor,
    not,
    bnot,
    xor,
    bxor,

    // keywords
    kw_if,
    kw_for,
    kw_in,
    kw_to,
    kw_up,
    kw_else,
    kw_return,
    kw_while,
    kw_struct,
    kw_union,
    kw_enum,

    // literals and identifiers
    identifier,
    number,
    bitnumber,
    floatnumber,
    hexnumber,
    string,
};

pub const Token = struct {
    tag: token,
    loc: struct { col: usize, line: usize },
    text: []const u8,
};

const token_map = std.StaticStringMap(token).initComptime(.{
    .{ "if", .kw_if },
    .{ "for", .kw_for },
    .{ "in", .kw_in },
    .{ "to", .kw_to },
    .{ "up", .kw_up },
    .{ "else", .kw_else },
    .{ "return", .kw_return },
    .{ "while", .kw_while },
    .{ "struct", .kw_struct },
    .{ "union", .kw_union },
    .{ "enum", .kw_enum },

    .{ "and", .and_ },
    .{ "band", .band },
    .{ "or", .or_ },
    .{ "bor", .bor },
    .{ "not", .not },
    .{ "bnot", .bnot },
    .{ "xor", .xor },
    .{ "bxor", .bxor },
    .{ "[[", .leftattr },
    .{ "]]", .rightattr },

    .{ "{", .leftbrace },
    .{ "}", .rightbrace },
    .{ "(", .leftparen },
    .{ ")", .rightparen },
    .{ "[", .leftbracket },
    .{ "]", .rightbracket },

    .{ ":", .colon },
    .{ ";", .semi },
    .{ ",", .comma },
    .{ "=", .equals },
    .{ ".", .dot },
    .{ "*", .asterisk },
    .{ "+", .plus },
    .{ "-", .minus },
    .{ "|", .bar },
    .{ ">", .greater },
    .{ "<", .less },
    .{ "$", .dollar },
    .{ "&", .ampersand },
    .{ "#", .hash },
    .{ "==", .equalequal },
    .{ "!=", .notequal },
    .{ "<=", .lessequal },
    .{ ">=", .greaterequal },
    .{ "+=", .plusequal },
    .{ "-=", .minusequal },
    .{ "|>", .pipe },
    .{ "|>=", .pipeequal },
    .{ "/", .slash },
    .{ "\\", .bslash },
});

pub fn tokenizeFile(alloc: Allocator, file: File) !struct { []Token, []const u8 } {
    const source = label: {
        var io = std.Io.Threaded.init_single_threaded;
        defer io.deinit();
        var reader = file.reader(io.io(), &.{});
        const len = try reader.getSize();
        const result = try alloc.alloc(u8, len);
        _ = try file.readPositionalAll(io.io(), result, 0);
        break :label result;
    };

    var toks = try std.ArrayListUnmanaged(Token).initCapacity(alloc, 256);
    errdefer toks.deinit(alloc);

    var i: usize = 0;
    var line: usize = 0;
    var line_start: usize = 0;

    while (i < source.len) {
        while (i < source.len and std.ascii.isWhitespace(source[i])) {
            if (source[i] == '\n') {
                line += 1;
                line_start = i + 1;
            }
            i += 1;
        }
        if (i >= source.len) break;

        const c = source[i];
        const start = i;
        const current_loc: @TypeOf(@as(Token, undefined).loc) = .{ .col = start - line_start, .line = line };

        if (c == '/' and i + 1 < source.len and source[i + 1] == '/') {
            while (i < source.len and source[i] != '\n') i += 1;
            continue;
        }

        if (c == '"') {
            i += 1;
            while (i < source.len) {
                if (source[i] == '\\') {
                    i += 1;
                } else if (source[i] == '"') {
                    i += 1;
                    break;
                } else if (source[i] == '\n') {
                    line += 1;
                    line_start = i + 1;
                }
                i += 1;
            }
            try toks.append(alloc, .{ .tag = .string, .loc = current_loc, .text = source[start..i] });
            continue;
        }

        if (std.ascii.isDigit(c)) {
            if (c == '0' and i + 1 < source.len) {
                if (source[i + 1] == 'b') {
                    i += 2;
                    while (i < source.len and (source[i] == '0' or source[i] == '1')) i += 1;
                    try toks.append(alloc, .{ .tag = .bitnumber, .loc = current_loc, .text = source[start..i] });
                    continue;
                } else if (source[i + 1] == 'x') {
                    i += 2;
                    while (i < source.len and std.ascii.isHex(source[i])) i += 1;
                    try toks.append(alloc, .{ .tag = .hexnumber, .loc = current_loc, .text = source[start..i] });
                    continue;
                }
            }

            var is_float = false;
            while (i < source.len and (std.ascii.isDigit(source[i]) or source[i] == '.')) {
                if (source[i] == '.') is_float = true;
                i += 1;
            }

            if (is_float) {
                try toks.append(alloc, .{ .tag = .floatnumber, .loc = current_loc, .text = source[start..i] });
            } else {
                try toks.append(alloc, .{ .tag = .number, .loc = current_loc, .text = source[start..i] });
            }
            continue;
        }
        const isValidIdPart = struct {
            fn f(ch: u8) bool {
                return std.ascii.isAlphabetic(ch) or std.ascii.isDigit(ch) or ch == '_' or ch == '@';
            }
        }.f;

        if (isValidIdPart(c)) {
            while (i < source.len and isValidIdPart(source[i])) i += 1;
        } else {
            var best_len: usize = 1;
            var check_len: usize = 1;

            while (i + check_len <= source.len) {
                const ch = source[i + check_len - 1];
                if (isValidIdPart(ch) or std.ascii.isWhitespace(ch)) break;
                if (token_map.has(source[i .. i + check_len])) best_len = check_len;
                check_len += 1;
            }
            i += best_len;
        }

        const text = source[start..i];
        if (token_map.get(text)) |tag| {
            try toks.append(alloc, .{ .tag = tag, .loc = current_loc, .text = text });
        } else try toks.append(alloc, .{ .tag = .identifier, .loc = current_loc, .text = text });
    }

    return .{
        try toks.toOwnedSlice(alloc),
        source,
    };
}

pub fn etoa(
    i: anytype,
) ?@typeInfo(@TypeOf(i)).error_union.payload {
    return i catch null;
}
pub fn bind(captures: anytype, comptime Namespace: type) struct {
    ctx: @TypeOf(captures),
    const rtype = @typeInfo(@TypeOf(Namespace.call)).@"fn".return_type orelse void;
    pub fn call(self: @This(), args: anytype) rtype {
        return @call(.auto, Namespace.call, .{self.ctx} ++ args);
    }
} {
    return .{ .ctx = captures };
}

pub const astNode = struct {
    next: ?*astNode = null,

    token: ?Token,
    type: ?*itemType = null,
    attributes: ?*attribute = null,
    node: nodeType,

    const nodeType = union(enum) {
        ident: void,
        initi: struct {
            name: ?*astNode,
            typ: ?*astNode,
            val: ?*astNode,
            isconst: bool,
        },

        //
        // possible values
        //

        op: struct {
            l: ?*astNode,
            op: void,
            r: ?*astNode,
        },

        call: struct {
            called: *astNode,
            args: ?*astNode,
        },

        func: struct {
            argslist: *astNode,
            rtype: *astNode,
            attrs: *astNode,
            body: *astNode,
        },

        block: struct {
            attrs: *astNode,
            body: *astNode,
        },
    };
    pub fn formatHelper(
        self: @This(),
        writer: *std.Io.Writer,
        depth: usize,
    ) std.Io.Writer.Error!void {
        {
            var deapth: usize = depth;
            const pdeapth = bind(.{ .deapth = &deapth, .writer = writer }, struct {
                fn call(slef: anytype) error{WriteFailed}!void {
                    for (0..slef.deapth.*) |_| {
                        try slef.writer.print("  ", .{});
                    }
                }
            });
            switch (self.node) {
                .ident => {
                    try writer.print("(identifier {s})", .{self.token.?.text});
                },
                .initi => |in| {
                    try writer.print("(initialization\n", .{});
                    defer {
                        pdeapth.call(.{}) catch {};
                        writer.print(")\n", .{}) catch {};
                    }

                    // Increase depth for all fields inside this node
                    deapth += 1;
                    defer deapth -= 1;

                    try pdeapth.call(.{});
                    const isconst = if (in.isconst) "const" else "var";
                    try writer.print("kind : {s}", .{isconst});

                    if (in.name) |name| {
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try writer.print("name :\n", .{});
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try formatHelper(name.*, writer, deapth);
                    }
                    if (in.typ) |typ| {
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try writer.print("type :\n", .{});
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try formatHelper(typ.*, writer, deapth);
                    }
                    if (in.val) |val| {
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try writer.print("val :\n", .{});
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try formatHelper(val.*, writer, deapth);
                    }
                },
                .op => |in| {
                    try writer.print("(operation\n", .{});
                    defer {
                        pdeapth.call(.{}) catch {};
                        writer.print(")\n", .{}) catch {};
                    }
                    deapth += 1;
                    defer deapth -= 1;

                    try pdeapth.call(.{});
                    try writer.print("kind : {}\n", .{self.token.?.tag});

                    if (in.l) |l| {
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try writer.print("left :\n", .{});
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try formatHelper(l.*, writer, deapth);
                    }
                    if (in.r) |r| {
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try writer.print("right :\n", .{});
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try formatHelper(r.*, writer, deapth);
                    }
                },
                .call => |in| {
                    try writer.print("(call\n", .{});
                    defer {
                        pdeapth.call(.{}) catch {};
                        writer.print(")\n", .{}) catch {};
                    }
                    deapth += 1;
                    defer deapth -= 1;

                    try pdeapth.call(.{});
                    try writer.print("funct :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try formatHelper(in.called.*, writer, deapth);
                        try writer.print("\n", .{});
                    }

                    if (in.args) |args| {
                        try pdeapth.call(.{});
                        try writer.print("args :\n", .{});

                        // List children get an extra level of indentation
                        deapth += 1;
                        defer deapth -= 1;
                        var node: ?*astNode = args;
                        while (node) |n| {
                            try pdeapth.call(.{});
                            try formatHelper(n.*, writer, deapth);
                            try writer.print("\n", .{});
                            node = n.next;
                        }
                    } else {
                        try pdeapth.call(.{});
                        try writer.print("args :\n", .{});
                        deapth += 1;
                        defer deapth -= 1;
                        try pdeapth.call(.{});
                        try writer.print("(none)\n", .{});
                    }
                },
                .func => |in| {
                    try writer.print("(func\n", .{});
                    defer {
                        pdeapth.call(.{}) catch {};
                        writer.print(")\n", .{}) catch {};
                    }
                    deapth += 1;
                    defer deapth -= 1;

                    try pdeapth.call(.{});
                    try writer.print("argslist :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try formatHelper(in.argslist.*, writer, deapth);
                    }

                    try pdeapth.call(.{});
                    try writer.print("rtype :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try formatHelper(in.rtype.*, writer, deapth);
                    }

                    try pdeapth.call(.{});
                    try writer.print("attrs :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try formatHelper(in.attrs.*, writer, deapth);
                    }

                    try pdeapth.call(.{});
                    try writer.print("body :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try formatHelper(in.body.*, writer, deapth);
                    }
                },
                .block => |in| {
                    try writer.print("(block\n", .{});
                    defer {
                        pdeapth.call(.{}) catch {};
                        writer.print(")\n", .{}) catch {};
                    }
                    deapth += 1;
                    defer deapth -= 1;

                    try pdeapth.call(.{});
                    try writer.print("attrs :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        try writer.print("\n", .{});
                        try pdeapth.call(.{});
                        try formatHelper(in.attrs.*, writer, deapth);
                    }

                    try pdeapth.call(.{});
                    try writer.print("body :\n", .{});
                    {
                        deapth += 1;
                        defer deapth -= 1;
                        var node: ?*astNode = in.body;
                        while (node) |n| {
                            try pdeapth.call(.{});
                            try formatHelper(n.*, writer, deapth);
                            try writer.print("\n", .{});
                            node = n.next;
                        }
                    }
                },
            }
        }
        if (self.next) |next|
            try formatHelper(next.*, writer, depth);
    }
    pub fn format(self: @This(), writer: *std.Io.Writer) std.Io.Writer.Error!void {
        return try self.formatHelper(writer, 0);
    }
};

const astNodeNode = astNode.nodeType;
pub const interpreter = struct {};
pub const parser = struct {
    alloc: std.heap.ArenaAllocator,
    palloc: Allocator,
    first: ?*astNode = null,
    tokenList: ?[]Token = null,
    sourceList: ?[]const u8 = null,

    pub fn init(alloc: Allocator) @This() {
        return .{
            .alloc = std.heap.ArenaAllocator.init(alloc),
            .palloc = alloc,
        };
    }

    pub fn deinit(self: *@This()) void {
        self.alloc.deinit();
        if (self.tokenList) |tl| self.palloc.free(tl);
        if (self.sourceList) |sl| self.palloc.free(sl);
    }

    fn parseError(T: type) type {
        return union(enum) {
            const errtypeInner = error{ unexpectedToken, unexpectedEOF, OutOfMemory };
            const errtype = struct { Token, errtypeInner };
            err: errtype,
            succ: T,
            pub fn sucess(t: T) @This() {
                return .{ .succ = t };
            }
            pub fn asTry(self: @This()) errtypeInner!T {
                switch (self) {
                    .err => |e| {
                        std.log.err("error at line : {} col : {} , {}", .{ e.@"0".loc.line, e.@"0".loc.col, e.@"1" });
                        return e.@"1";
                    },
                    .succ => |x| {
                        return x;
                    },
                }
            }
            pub fn errOf(err: errtypeInner, t: token) @This() {
                return .{
                    .err = .{
                        t,
                        err,
                    },
                };
            }
            pub fn noTok(e: errtypeInner) @This() {
                return errOf(
                    e,
                    .{
                        .tag = .identifier,
                        .text = "expected token",
                        .loc = .{ .col = 0, .line = 0 },
                    },
                );
            }
            pub fn oom() @This() {
                return .{
                    .err = .{
                        .{ .tag = .identifier, .text = "compiler is out of memory", .loc = .{ .col = 0, .line = 0 } },
                        error.OutOfMemory,
                    },
                };
            }
        };
    }

    const splitResult = parseError(struct { first: []const Token, rest: []const Token });
    fn split(
        self: @This(),
        comptime t: token,
        tokens: []const Token,
    ) splitResult {
        comptime {
            switch (t) {
                .leftattr,
                .leftbrace,
                .leftparen,
                .leftbracket,
                .rightattr,
                .rightbrace,
                .rightparen,
                .rightbracket,
                => {
                    @compileError("scope markers not allowed here");
                },
                else => {},
            }
        }
        const opposite = struct {
            pub fn f(tk: token) token {
                switch (tk) {
                    .leftattr,
                    .leftbrace,
                    .leftparen,
                    .leftbracket,
                    => {
                        return switch (tk) {
                            .leftattr => .rightattr,
                            .leftbrace => .rightbrace,
                            .leftparen => .rightparen,
                            .leftbracket => .rightbracket,
                            else => unreachable,
                        };
                    },
                    else => {
                        @panic("non-scope-begin markers not allowed here");
                    },
                }
            }
        }.f;
        const is_scope = struct {
            const is_scope_r = enum {
                left,
                right,
                isnt,
            };
            fn f(tk: token) is_scope_r {
                return switch (tk) {
                    .leftattr,
                    .leftbrace,
                    .leftparen,
                    .leftbracket,
                    => .left,
                    .rightattr,
                    .rightbrace,
                    .rightparen,
                    .rightbracket,
                    => .right,
                    else => .isnt,
                };
            }
        }.f;
        var stack = sList(token, 1024).init;
        defer stack.deinit(self.palloc);

        var i: usize = 0;
        w: while (i < tokens.len) {
            const tk = tokens[i].tag;
            switch (is_scope(tk)) {
                .left => stack.append(self.palloc, opposite(tk)) catch {
                    return splitResult.oom();
                },
                .right => {
                    if (stack.len > 0 and stack.last() == tk) {
                        _ = stack.pop();
                    } else return .{ .err = .{ tokens[i], error.unexpectedToken } };
                },
                .isnt => {
                    if (tk == t and stack.len == 0) break :w;
                },
            }
            i += 1;
        }
        if (i == tokens.len)
            return .{ .err = .{ tokens[0], error.unexpectedEOF } };
        return .{ .succ = .{
            .first = tokens[0..i],
            .rest = tokens[i + 1 ..],
        } };
    }

    const expectResult = parseError([]const Token);
    fn expect(
        self: @This(),
        comptime toks: []token,
        tokens: []const Token,
    ) expectResult {
        _ = self;
        if (tokens.len == 0) return expectResult.noTok(expectResult.errtypeInner.unexpectedToken);
        inline for (toks) |tok| {
            if (tokens[0].tag == tok)
                expectResult.sucess(.{tokens[1..]});
        }
        return expectResult.errOf(expectResult.errtypeInner.unexpectedToken, tokens[0]);
    }
    // fn indexOfTags(tokens: []const Token, comptime tags: []const token) ?usize {
    //     for (tokens, 0..) |tok, i|
    //         inline for (tags) |tag|
    //             if (tok.tag == tag) return i;
    //     return null;
    // }
    fn indexOfTagsBalanced(self: @This(), tokens: []const Token, comptime target_tags: []const token) error{ OutOfMemory, unexpectedToken }!?usize {
        comptime {
            for (target_tags) |t|
                switch (t) {
                    .leftattr,
                    .leftbrace,
                    .leftparen,
                    .leftbracket,
                    .rightattr,
                    .rightbrace,
                    .rightparen,
                    .rightbracket,
                    => {
                        @compileError("scope markers not allowed here");
                    },
                    else => {},
                };
        }
        const is_scope = struct {
            const is_scope_r = enum { left, right, isnt };
            fn f(tk: token) is_scope_r {
                return switch (tk) {
                    .leftattr, .leftbrace, .leftparen, .leftbracket => .left,
                    .rightattr, .rightbrace, .rightparen, .rightbracket => .right,
                    else => .isnt,
                };
            }
        }.f;

        const opposite = struct {
            fn f(tk: token) token {
                return switch (tk) {
                    .leftattr => .rightattr,
                    .leftbrace => .rightbrace,
                    .leftparen => .rightparen,
                    .leftbracket => .rightbracket,
                    else => unreachable,
                };
            }
        }.f;

        var stack = sList(token, 1024).init;
        defer stack.deinit(self.palloc);

        for (tokens, 0..) |tok, i| {
            const tk = tok.tag;
            switch (is_scope(tk)) {
                .left => try stack.append(self.palloc, opposite(tk)),
                .right => {
                    if (stack.len > 0 and stack.last() == tk) {
                        _ = stack.pop();
                    } else return error.unexpectedToken;
                },
                .isnt => {
                    if (stack.len == 0) {
                        inline for (target_tags) |target| {
                            if (tk == target) return i;
                        }
                    }
                },
            }
        }
        return null;
    }
    fn makeIdentifier(self: *@This(), t: Token) !*astNode {
        std.debug.assert(t.tag == .identifier);
        const result = try self.alloc.allocator().create(astNode);
        result.* = .{ .token = t, .node = .ident };
        return result;
    }
    fn parseType(_: []const Token) !?*astNode {
        return null;
    }
    fn parseVal(_: []const Token) !?*astNode {
        return null;
    }

    pub fn parse_initialization(self: *@This(), tokenList: []Token) !struct { *astNode, []Token } {
        const splitn = try self.split(.semi, tokenList).asTry();

        const thisPart = splitn.first;
        const resultSlice = splitn.rest;

        var name_node: ?*astNode = null;
        var typ_node: ?*astNode = null;
        var val_node: ?*astNode = null;
        var isconst = true;

        const isDec = if (try self.indexOfTagsBalanced(thisPart, &.{.colon})) |_| true else false;
        if (isDec) {
            const a = try self.split(.colon, thisPart).asTry();
            // a0 : a1
            const b = try self.indexOfTagsBalanced(a.rest, &.{ .colon, .equals });
            if (b) |idx| { // :|= , initialization
                isconst = a.rest[idx].tag == .colon;
                const tv = try (if (isconst) self.split(.colon, a.rest) else self.split(.equals, a.rest)).asTry();
                typ_node = try parseType(tv.first);
                val_node = try parseVal(tv.rest);
            } else { // declaration
                typ_node = try parseType(a.rest);
            }
            std.debug.assert(a.first.len <= 1);
            if (a.first.len == 1) name_node = try self.makeIdentifier(a.first[0]);
        } else {
            val_node = try parseVal(thisPart);
        }
        const result = try self.alloc.allocator().create(astNode);
        result.* = .{
            .token = tokenList[0],
            .node = .{ .initi = .{
                .name = name_node,
                .val = val_node,
                .typ = typ_node,
                .isconst = isconst,
            } },
        };
        return .{ result, @constCast(resultSlice) };
    }
    pub fn parse(self: *@This(), file: File) !*astNode {
        const both = try tokenizeFile(self.palloc, file);
        self.tokenList = both.@"0";
        self.sourceList = both.@"1";

        var current = self.tokenList.?;
        var first_node: ?*astNode = null;
        var last_node: ?*astNode = null;

        while (current.len > 0) {
            const result = try self.parse_initialization(current);
            const init_node = result.@"0";
            current = result.@"1";

            first_node = first_node orelse init_node;

            if (last_node) |last|
                last.next = init_node;
            last_node = init_node;
        }

        self.first = first_node;
        return first_node orelse error.emptyFile;
    }
};
