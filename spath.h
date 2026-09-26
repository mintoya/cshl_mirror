#pragma once
#include "wheels/mylist.h"
typedef mList(cstr) spath;
constexpr char joiner =
#if __has_include(<windows.h>)
    '\\'
#else
    '/'
#endif
    ;
cstr spath_strldup(allocfn allocator, const cstr seg, usize len) {
  let res = acreate(allocator, char[len + 1]);
  memcpy(*res, seg, len);
  (*res)[len] = 0;
  return *res;
}
#define spath_freeSeg(alloc, seg) adestroy((alloc), (char (*)[strlen(seg) + 1])(seg))
void spath_join(spath path, const cstr name) {
  mList_push(path, spath_strldup(mList_allocator(path), name, strlen(name)));
}
cstr spath_tostr(allocfn allocator, spath path) {
  let len = mList_len(path);
  bool absolute = len && mList_arr(path)[0][0] == 0;
  usize size = absolute;
  foreach (let i, mList_iter(path))
    size += strlen(i) + 1;
  let res = *acreate(allocator, char[size + 1]);
  usize at = 0;
  if (absolute)
    res[at++] = joiner;
  foreach (let i, range((usize)absolute, len)) {
    if (at && res[at - 1] != joiner)
      res[at++] = joiner;
    usize n = strlen(mList_arr(path)[i]);
    memcpy(res + at, mList_arr(path)[i], n);
    at += n;
  }
  res[at] = 0;
  return res;
}
spath spath_init(allocfn allocator, char *path) {
  return mList_initArr(allocator, (char *[1]){spath_strldup(allocator, path, strlen(path))});
}
void spath_deinit(spath path) {
  foreach (let p, mList_iter(path))
    spath_freeSeg(mList_allocator(path), p);
  mList_deinit(path);
}
void spath_normalize(spath path) {
  let allocator = mList_allocator(path);
  let l = mList_len(path);
  foreach (let i, range(0, l)) {
    cstr element = mList_arr(path)[i];
    cstr begin = element;
    for (cstr p = element;; p++) {
      if (*p && *p != joiner)
        continue;
      bool done = *p == 0;
      if (p > begin)
        mList_push(path, spath_strldup(allocator, begin, (usize)(p - begin)));
      else if (p == element && !done) // leading '/' -> absolute root
        mList_push(path, spath_strldup(allocator, "", 0));
      if (done)
        break;
      begin = p + 1;
    }
    spath_freeSeg(allocator, element);
  }
  mList_reml(path, 0, l);

  usize i = 0;
  while (i < mList_len(path)) {
    cstr s = mList_arr(path)[i];
    if (streq(s, ".")) {
      spath_freeSeg(allocator, s);
      mList_rem(path, i);
    } else if (streq(s, "..") && i && !streq(mList_arr(path)[i - 1], "..")) {
      cstr prev = mList_arr(path)[i - 1];
      spath_freeSeg(allocator, s);
      mList_rem(path, i);
      if (prev[0]) { // a real component to pop
        spath_freeSeg(allocator, prev);
        mList_rem(path, i - 1);
        i--;
      }
    } else i++;
  }
  if (!mList_len(path))
    mList_push(path, spath_strldup(allocator, ".", 1));
}
#undef spath_freeSeg

NAMESPACE_STRUCT(
    SPath,
    (init, &spath_init),
    (join, &spath_join),
    (tostr, &spath_tostr),
    (deinit, &spath_deinit),
    (normalize, &spath_normalize),
);

