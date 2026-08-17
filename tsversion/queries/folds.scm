; Fold standard bracket blocks
(bblock) @fold
(ablock) @fold
(pblock) @fold

; Optionally fold nblocks if they span multiple lines
(nblock) @fold

; Prevent folding a single-line block (optional but highly recommended)
; Neovim respects standard vim settings here, but you can force it in the query:
(#set! fold.minlines 2)
