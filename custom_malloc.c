/*
Rapidly, the main take is to make a custom allocator which allocates particular quantity of memory
for each type of data, i mean if you allocate memory for char and write malloc(2) it means that you allocated
2 bytes, taking in consideration that each char takes 1 byte of memory this allocation gives you a storage for
2 chars, if you try to make int *a = malloc(2) it will fails cause int is a 4 byte type 
(classicaly (no speach ) about int32_t e.t.c), so the target is to make something like if you write:
int* a = custom_malloc(2) it will give you totally 4*2=8 bytes for 2 int objects. We will use the
enhanced method of first matching
*/
