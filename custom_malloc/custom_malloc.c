#include "custom_malloc.h"

/*
    Rapidly, the main take is to make a custom allocator which allocates particular quantity of memory
    for each type of data, i mean if you allocate memory for char and write malloc(2) it means that you allocated
    2 bytes, taking in consideration that each char takes 1 byte of memory this allocation gives you a storage for
    2 chars, if you try to make int *a = malloc(2) it will fails cause int is a 4 byte type 
    (classicaly (no speach ) about int32_t e.t.c), so the target is to make something like if you write:
    int* a = custom_malloc(2) it will give you totally 4*2=8 bytes for 2 int objects. We will use the
    enhanced method of first matching

    Основная задача аллокатора памяти в общих чертах - это:
        1)Определить, хватает ли памяти, которую хочет выделить программист
        2)Получить секцию из памяти досутпной для использования
        3)В си память не возвращается непосредственно в ОС, а возвращается в так называем free_list
        из этого фрилиста можно брать память и реиспользовать ее, главное выбрать верный алгоритм поиска нужных
        фрагментов - в нашем случае - это метод первого подходящего

*/

/*
Для начала создадим глобальные переменные, которые будут содержать данные для алокатора 
(инициализирован/нет),указательна последний адрес,
который можно выделить и указатель который будет установлен на начло выделяемой памяти
*/ 
int init_flag=0;
void* border_of_occupied_memory_ptr;
void* allocated_memory_ptr;

/*
    Важно понимать, что классический malloc не просто берет и расширяет количссетво памяти в куче,
    а сначала ищет, есть ли достаточный по обьему фрагмент памяти, свободный при этом, который можно
    выделить не расширяя кучу, для этого нужны некоторые метаданные: во первых выделяя память в первой ячеке 
    мы и будем хранить метаданные: 1)TAG-свободен ли данный фрагмент 2) размер этого фрагмента 
    (и мб в дальнейшем указатель на следующий фрагмент памяти)<-------- если будем использовать mmap
*/
typedef struct metadata{
    int if_available; // tag - свободен ли блок для записи или нет
    size_t size; // размер всего блока header + payload + footer
}metadata_t;

typedef struct metadata_end{
    int if_available;
}metadata_end_t;
/*
    В слуае свободного блока вмсто полезной нагрузки в память мы будем помещать указатель на следующий 
    свободный блок и предыдущий свободный блок, тем самым мы как бы получим двусвязный список, тем самым
    ,где бы в памяти не был следующий или предыдущий свободный блок, мы его найдем, тем более, нам не надо
    при поиске свободного блока перебирать все существующие блоки, мы лишь найдем первый, дальше пробежимся
    по всем блокам, через двусвязный список
*/
typedef struct freelist{
    struct freelist *prev;
    struct freelist *next;
}free_list_t;

/*
    Мы будем использовать для аллокации mmap,а не sbrk - возвращает систем брейк, 
    т.е. крайнюю границу размеченной для процесса памяти,brk - просто смещает эту границу (т.е.
    память линейно выделяется, что не оень хорошо)

    Далее, поскольку у нас при выделении каждого блока выделяется не ровно стоько памяти, сколько
    запросил памяти пользователь, а еще и память для метаданных в начале выделенного блока, а так 
    же для метаднных в конце блока, нужно написать несколько функций, которые получают указатель на полезную
    нагрузку, которые получат указатель на метаданные в конце, функция для добавления во freelist, функция
    по удалению из freelist, далее могут быть добавлены еще несколько функций


    Т.е логика выделения такова:
        1)Занятый блок:
            |header(метаданные начала)|payload(полезная нагрузка)|footer(метаданные конца)|
        2)Свободный блок:
            |header(метаданные начала)|next* and prev* (указатели на след и пред свобоные блоки)|footer(метаданные конца)|
*/
#define HEADER_SZ sizeof(metadata_t)
#define FOOTER_SZ sizeof(metadata_end_t)
//Функция для получения указателя на полезную нагрузку в блоке (фрагменте памяти), указатель на начало которого - ptr
//т.е ptr - указатель на header
void* get_ptr_to_payload(void* ptr){
    return ptr + HEADER_SZ;
}
//Получаем указатель на footer (ptr - указатель на header)
void* get_ptr_to_footer(void* ptr){
    return ptr + HEADER_SZ + ((metadata_t*)ptr)->size - FOOTER_SZ;
}
int main (void){
    metadata_t a;
    metadata_end_t b;
    printf("%lu,%lu",sizeof(a),sizeof(b));
}
