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

//тут все аналогично
typedef struct metadata_end{
    int if_available;
    size_t size;
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
}freelist_t;

freelist_t* freelist_first_item = NULL;
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
#define IN_USE 1
#define VACANT 0
//Функция для получения указателя на полезную нагрузку в блоке (фрагменте памяти), указатель на начало которого - ptr
//т.е ptr - указатель на header
void* get_ptr_to_payload(void* ptr){
    return ptr + HEADER_SZ;
}
//Получаем указатель на footer (ptr - указатель на header)
void* get_ptr_to_footer(void* ptr){
    return ptr + HEADER_SZ + ((metadata_t*)ptr)->size - FOOTER_SZ;
}
/*
    Функция, чтобы менять состояние блока (занят/свободен)

    ptr - указатель на начало блока (header); acces_status - флаг доступа (свободен или занят)
    дело в том, что выделяем мы память не взирая на то, какой тип данных нужно поместить туда
    (любой аллокатор возвращает void*, который кастится в любой тип), т.е. зная указатель на весь блок
    мы спокойно можем кастить его в структуры метаданных, потому что, например по адресу начала нашего блока
    будет располагаться именно структура metadata_t, а подходя к концу выделенного блока, и зная о том, что
    в конце дожен быть footer (metadata_end_t) мы получии с помощью специально функции указатель на то место,где
    должен быть footer и прокастим его в вышеуказанный тип структуры metada_end_t, тем самым ,после каста мы сможем получить
    доступ к элементам footer,по указателю на начало всего блока и имеющего вообще тип void*.
*/
void set_tag_of_access(void* ptr,int access_status){
    ((metadata_t*)ptr)->if_available = access_status;
    metadata_end_t* footer = (metadata_end_t*)get_ptr_to_footer(ptr);
    footer->if_available = access_status;
    footer->size = ((metadata_t*)ptr)->size;

}
/*
    Далее пойдет функция для работы с фрилистом: при новом выделении памяти, мы сначала будем проверять, а не
    существует ли блока, который имеет примерно такой же размер, какой запрашивается пользователем, и если такой блок
    уже имеется, то смысла запрашивать новую память у системы нет, это сэкономит ресурсы ОС. ptr - указатель на начало блока
*/
int check_size_of_free_list_instance(void* ptr){
    int size = ((metadata_t*)ptr)->size;
    return size;
}
/*
    Далее естественно будут нужны функции для добавления фрагментов памяти во фрилист и, соответственно, удаления
    их от туда, ptr - указатель на начало выделенного блока памяте (на его header)
*/
void add_to_free_list(void* ptr){
    set_tag_of_access(ptr,VACANT);
    freelist_t new_freelist_block;
    /*
        Получим указатель на саму память в блоке предназначенную для полезной нагрузки
        туда мы и поместим структуру данных фрилиста, где содержаться указатели на следующий свободный
        блок и предыдущий
    */
    freelist_t* new_freelist_block_ptr = get_ptr_to_payload(ptr);
    if(!freelist_first_item){
        //Если рассматриваемый блок памяти первый, помещенный во фрилист, то не следующего, не предыдущего блока не будет
        freelist_first_item = new_freelist_block_ptr;
        new_freelist_block_ptr->prev = NULL;
        new_freelist_block_ptr->next = NULL;
    }
    else{
        /*
        freelist работает по принципо FIFO (First in first out), 
        т.е. первый попавший во фрилст блок будет в начале,т.е. каждый новый блок мы записываем в начало.
        FIFO предпочтительнее поскольку с точки зрения процессора последний высвобожденный блок еще находится в кеше
        то есть, с ним коммуникация будеь быстрее, чем с блоком, который был освобожден до этого.
        */
       new_freelist_block_ptr->next = freelist_first_item;
       new_freelist_block_ptr->prev = NULL;
       freelist_first_item->prev = new_freelist_block_ptr;
       //ну и теперь первый элемент в двусвязном списке сменился, то есть надо его сменить
       freelist_first_item = new_freelist_block_ptr;
    }
}
/*
    Тут мы удаляем из фрилиста, то есть имеем дело со свободными блоками 
    (т.е знаем что в памяти для полезной нагрузки) содержится структура freelist_t
    , надо сменить линковку (то есть переобозначить по новой указатели)
*/
void delete_from_free_list(void* ptr){
    set_tag_of_access(ptr,IN_USE);
    freelist_t* block_to_be_freed = (freelist_t*)get_ptr_to_payload(ptr);

    freelist_t* next_free_block = block_to_be_freed ->next;
    freelist_t* prev_free_block = block_to_be_freed ->prev;

    if(!prev_free_block){
        if(!next_free_block){
            //То есть фрилист состоит только из одного элмента, с которым мы и имеем дело
            freelist_first_item = NULL;
        }
        else{
            /*
                То есть мы имеем дело с первым блоком во фрилисте
            */
            freelist_first_item = next_free_block;
            /*
                Ну и поскольку теперь следующий блок - это первый блок во фрилисте,
                 то у него не должо быть предыдущего.
            */
            (freelist_first_item)->prev = NULL;
        }
        
    }
    else{
        if(!next_free_block){
            //То есть если нет следующего блока во фрилисте (имеем дело с последним блоком фрилиста)
            prev_free_block->next = NULL;
        }
        else{
            next_free_block->prev = prev_free_block;
            prev_free_block->next = next_free_block;
        }
    }
}

int main (void){
    metadata_t a;
    metadata_end_t b;
    printf("%lu,%lu",sizeof(a),sizeof(b));
}
