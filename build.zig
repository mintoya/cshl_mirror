const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{ .preferred_optimize_mode = .Debug });
    const slist = b.addModule("slist", .{
        .root_source_file = b.path("src/smallList.zig"),
        .target = target,
        .optimize = optimize,
    });
    const mod = b.addModule("zl", .{
        .root_source_file = b.path("src/root.zig"),
        .target = target,
        .optimize = optimize,
    });
    mod.addImport("slist", slist);

    const clap = b.dependency("clap", .{});
    const exe = b.addExecutable(.{
        .name = "zl",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "zl", .module = mod },
                .{ .name = "clap", .module = clap.module("clap") },
                .{ .name = "slist", .module = slist },
            },
        }),
    });

    b.installArtifact(exe);

    const run_step = b.step("run", "Run the app");

    const run_cmd = b.addRunArtifact(exe);
    run_step.dependOn(&run_cmd.step);

    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args|
        run_cmd.addArgs(args);

    const mod_tests = b.addTest(.{
        .root_module = mod,
    });

    const run_mod_tests = b.addRunArtifact(mod_tests);

    const exe_tests = b.addTest(.{
        .root_module = exe.root_module,
    });

    const run_exe_tests = b.addRunArtifact(exe_tests);

    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&run_mod_tests.step);
    test_step.dependOn(&run_exe_tests.step);

    const exe_check = b.addExecutable(.{
        .name = "zl",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "zl", .module = mod },
                .{ .name = "clap", .module = clap.module("clap") },
                .{ .name = "slist", .module = slist },
            },
        }),
        .use_lld = false,
        .use_llvm = false,
    });
    const check_step = b.step("check", "Check compilation (useful for zls/CI)");
    check_step.dependOn(&exe_check.step);
}
