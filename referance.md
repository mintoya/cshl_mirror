i am not implementing rvalues, instead i am using referances here, they are exactly the same as a pointer, and they are pointer sized, but at the point of use they are implicitly dereferanced.
```cshl
i ~i32= 1;
// j ~(@refT i)= (@ref i);
j ~(@refT i) = i; //equivalent as well
j = 6;
j = j+1;
```
```c
int i = 1;
int* j= &i;
(*j) = 6;
(*j) = (*j) + 1;
```

