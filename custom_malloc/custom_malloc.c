#include "custom_malloc.h"

/*
Rapidly, the main take is to make a custom allocator which allocates particular quantity of memory
for each type of data, i mean if you allocate memory for char and write malloc(2) it means that you allocated
2 bytes, taking in consideration that each char takes 1 byte of memory this allocation gives you a storage for
2 chars, if you try to make int *a = malloc(2) it will fails cause int is a 4 byte type 
(classicaly (no speach ) about int32_t e.t.c), so the target is to make something like if you write:
int* a = custom_malloc(2) it will give you totally 4*2=8 bytes for 2 int objects. We will use the
enhanced method of first matching
*/
/*
    Основная задача аллокатора памяти в общих чертах - это:
        1)Определить, хватает ли памяти, которую хочет выделить программист
        2)Получить секцию из памяти досутпной для использования
        3)В си память не возвращается непосредственно в ОС, а возвращается в так называем free_list
        из этого фрилиста можно брать память и реиспользовать ее, главное выбрать верный алгоритм поиска нужных
        фрагментов - в нашем случае - это метод первого подходящего

*/

/*Для начала создадим глобальные переменные, которые будут содержать данные для алокатора 
(инициализирован/нет),указательна последний адрес,
который можно выделить и указатель который будет установлен на начло выделяемой памяти
*/ 
int init_flag=0;
void* border_of_occupied_memory_ptr;
void* allocated_memory_ptr;

/*Важно понимать, что классический malloc не просто берет и расширяет количссетво памяти в куче,
а сначала ищет, есть ли достаточный по обьему фрагмент памяти, свободный при этом, который можно
выделить не расширяя кучу, для этого нужны некоторые метаданные: во первых выделяя память в первой ячеке 
мы и будем хранить метаданные: 1)TAG-свободен ли данный фрагмент 2) размер этого фрагмента 
(и мб в дальнейшем указатель на следующий фрагмент памяти)<-------- если будем использовать mmap
*/
struct metadata{
    int if_available;
    size_t size;
};

/*Мы будем использовать для аллокации не mmap,а sbrk - возвращает систем брейк, 
т.е. крайнюю границу размеченной для процесса памяти,brk - просто смещает эту границу
*/
void custom_allocation_init(void){
    //забираем границу занятой процессом памяти (последний валидный адрес)
    border_of_occupied_memory_ptr = sbrk(0);
    //мы лишь инициалиируем алокатор и пока что не управляем никакой памятью,ставим указатель на последний
    //валидный адрес
    allocated_memory_ptr = border_of_occupied_memory_ptr;
    //Все готов и инициализировано, то есть флаг можно поднять
    init_flag = 1;
}
void custom_malloc(size_t bytes){
    if(!init_flag){
        custom_allocation_init();
    }
}