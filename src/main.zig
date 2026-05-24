const std = @import("std");
const print = std.debug.print;
const Io = std.Io;
const Dir = Io.Dir;
const Allocator = std.mem.Allocator;

const zl = @import("zl");
const clap = @import("clap");

pub fn bind(captures: anytype, comptime Namespace: type) struct {
    ctx: @TypeOf(captures),
    const rtype = @typeInfo(@TypeOf(Namespace.call)).@"fn".return_type orelse void;
    pub fn call(self: @This(), args: anytype) rtype {
        return @call(.auto, Namespace.call, .{self.ctx} ++ args);
    }
} {
    return .{ .ctx = captures };
}

pub fn main(init: std.process.Init) !void {
    var res = parseargs: {
        const params = comptime clap.parseParamsComptime(
            \\-h, --help             Display this help and exit.
            \\-S, --sexp          parse file as s-expressions
            \\<str>...
            \\
        );
        var diag = clap.Diagnostic{};
        const parsed = clap.parse(clap.Help, &params, clap.parsers.default, init.minimal.args, .{
            .diagnostic = &diag,
            .allocator = init.gpa,
        }) catch |err| {
            try diag.reportToFile(init.io, .stderr(), err);
            std.process.exit(1);
        };
        break :parseargs parsed;
    };
    defer res.deinit();

    _ = res.args.sexp == 0;

    const positionals: []const []const u8 = res.positionals[0];

    if (positionals.len < 1) {
        print("need at least one positional", .{});
        std.process.exit(1);
    }

    const cwd = Dir.cwd();
    var file = try Dir.openFile(cwd, init.io, positionals[0], .{ .allow_directory = false });
    defer file.close(init.io);

    var parser = zl.parser.init(init.gpa);
    defer parser.deinit();

    const tree = try parser.parse(file);
    print("{f}", .{tree});
}
