const std = @import("std");
const Allocator = std.mem.Allocator;

pub fn SmallList(comptime T: type, comptime cap: usize) type {
    return struct {
        len: usize,
        mem: union(enum) {
            heap: struct {
                mem: []T,
            },
            stack: struct {
                mem: [cap]T,
            },
        },

        pub const init: @This() =
            .{
                .len = 0,
                .mem = .{ .stack = .{ .mem = undefined } },
            };

        pub fn deinit(self: *@This(), alloc: Allocator) void {
            switch (self.mem) {
                .heap => |h| alloc.free(h.mem),
                .stack => {},
            }
        }

        fn active(self: *@This()) []T {
            return switch (self.mem) {
                .heap => self.mem.heap.mem[0..self.len],
                .stack => self.mem.stack.mem[0..self.len],
            };
        }
        pub fn push(self: *@This(), alloc: Allocator, value: T) !*T {
            try self.ensureCapacity(alloc, self.len + 1);
            self.len += 1;
            const ptr = &self.active()[self.len - 1];
            ptr.* = value;
            return ptr;
        }
        pub fn append(self: *@This(), alloc: Allocator, value: T) !void {
            _ = try self.push(alloc, value);
        }

        pub fn pushSlice(self: *@This(), alloc: Allocator, items: []const T) !void {
            if (items.len == 0) return;

            const needed = self.len + items.len;
            try self.ensureCapacity(alloc, needed);

            const start = self.len;
            self.len += items.len;

            const dst = self.active();
            @memcpy(dst[start..self.len], items);
        }
        fn ensureCapacity(self: *@This(), alloc: Allocator, needed: usize) !void {
            switch (self.mem) {
                .heap => |*h| {
                    if (h.mem.len < needed) {
                        var new_cap = if (h.mem.len == 0) cap else h.mem.len;
                        while (new_cap < needed) new_cap *= 2;
                        h.mem = try alloc.realloc(h.mem, new_cap);
                    }
                },
                .stack => |s| {
                    if (needed > cap) {
                        var new_cap: usize = cap;
                        while (new_cap < needed) new_cap *= 2;

                        var nbuf = try alloc.alloc(T, new_cap);
                        @memcpy(nbuf[0..self.len], s.mem[0..self.len]);

                        self.mem = .{ .heap = .{ .mem = nbuf } };
                    }
                },
            }
        }

        pub fn pop(self: *@This()) T {
            if (self.len < 1) @panic("OOB pop from SmallList");
            const res = self.active()[self.len - 1];
            self.len -= 1;
            return res;
        }

        pub fn popFail(self: *@This()) !T {
            if (self.len < 1) return error.OOB;
            const res = self.active()[self.len - 1];
            self.len -= 1;
            return res;
        }

        pub fn get(self: *@This(), i: usize) *T {
            if (i >= self.len) @panic("OOB read from SmallList");
            return &self.active()[i];
        }

        pub fn toOwned(self: *@This(), alloc: Allocator) ![]T {
            switch (self.mem) {
                .heap => |h| {
                    const res = h.mem[0..self.len];
                    self.* = init;
                    return res;
                },
                .stack => |s| {
                    const res = try alloc.alloc(T, self.len);
                    @memcpy(res, s.mem[0..self.len]);
                    self.len = 0;
                    return res;
                },
            }
        }
        pub fn last(self: *@This()) T {
            if (self.len < 1) @panic("OOB read from SmallList");
            return self.active()[self.len - 1];
        }
    };
}
