build commands : 
```sh
cc nob.c -o nob
./nob
```
on windows, a program cant be deleted if it's running i think? 
if you get an for that
also change all the 'nob' to 'nob.exe'
```sh
rm nob
```
then run it again, and also if you ever modify nob you have to remove and recompile?

# CSHL
---
meant to be a c-like language with
- first class types
- first class functions
- first class ast
- operator overloading
- raii?
## grammer
- empty  : `;`
- id     : identifier, c rules but can contain `!?$#`
- number : 
    - decimal : no prefix or `-`,0-9
    - hex     : `0x`,0-f
    - binary  : `0b`,0-1
- string : same c rules
- decl   :
    - full
        - mutable   : `name ~ type = value`
        - immutable : `name ~ type ~ value`
    - name  : can be an id, or some operation that yields an id
    - type  : full expression that should yield a type, optional
    - value : expression, optional if there is a type
- pblock :  `( ... )`, optional terminator, comma-separated
- bblock :  `{ ... }`, semicolon separated, non-optional terminator
- ablock :  `[ ... ]`, same as pblock
