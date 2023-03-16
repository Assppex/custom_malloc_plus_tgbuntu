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

int last_size_of_mmaping = 1 ; //см ниже что это
int* last_addres = 0;
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

#define MIN_SOTERED_SIZE sizeof(freelist_t)+10

#define PAGE_SIZE sysconf(_SC_PAGESIZE) //получаем размер страницы в байтах
#define CEIL(X) ((X - (int)(X)) > 0 ? (int)(X + 1) : (int)(X))
#define PAGES(size) (CEIL(size / (double)PAGE_SIZE))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

//Функция для получения указателя на полезную нагрузку в блоке (фрагменте памяти), указатель на начало которого - ptr
//т.е ptr - указатель на header
void* get_ptr_to_payload(void* ptr){
    return ptr + HEADER_SZ;
}
//Получаем указатель на footer (ptr - указатель на header)
void* get_ptr_to_footer(void* ptr){
    return ptr + ((metadata_t*)ptr)->size - FOOTER_SZ;
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
    if(freelist_first_item==NULL){
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
    write(STDOUT_FILENO, "0", strlen("0")); 
    set_tag_of_access(ptr,IN_USE);
    freelist_t* block_to_be_freed = (freelist_t*)get_ptr_to_payload(ptr);

    freelist_t* next_free_block =(freelist_t*)(block_to_be_freed ->next);
    freelist_t* prev_free_block = (freelist_t*)(block_to_be_freed ->prev);
    if(prev_free_block==NULL || freelist_first_item==NULL){
        if(next_free_block==NULL || freelist_first_item==NULL){
            //То есть фрилист состоит только из одного элмента, с которым мы и имеем дело
            freelist_first_item = NULL;
            write(STDOUT_FILENO, "3", strlen("3")); 
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
        if(next_free_block==NULL){
            write(STDOUT_FILENO, "2", strlen("2")); 
            //То есть если нет следующего блока во фрилисте (имеем дело с последним блоком фрилиста)
            prev_free_block->next = NULL;
        }
        else{
            write(STDOUT_FILENO, "1", strlen("1")); 
            next_free_block->prev = prev_free_block;
            prev_free_block->next = next_free_block;
            
        }
    }
}
/*
    Теперь функция поиска свободных блоков с передаваемым размером, возвращаем указатель именно сразу на
    header свободного блока
*/
void* search_for_freeblock(int requested_size){
    freelist_t* current_free_block = freelist_first_item;
    while(current_free_block){
        if(check_size_of_free_list_instance(current_free_block)>=requested_size){
            void* header_ptr = current_free_block -HEADER_SZ;
            return header_ptr;
        }
        else{
            current_free_block = current_free_block->next;
        }
    }
    return NULL;
}
/*
    Иногда так выходит, что во фрилисте есть блок ,сильно превышающий размер, запрошенный пользователем
    поэтому, чтобы избежать утечек памяти или ее нерационального расхода, надо делить блок на тот, что
    по размеру такой как запросил пользователь и остаток, который останется во фрилисте, то есть надо опять
    переназначать линковку двусвязного списка.

    Однако надо понимать, что делить блок на 2 не всегда возможно, т.к. как минимум оставшийся свободный блок
    должен вместить в себя структуру freelist_t (ведь эта оставшаяся часть во фрилисте остается), то есть должен
    быть некоторый минимальный размер MIN_STORED_SIZE для этого и введен

    Деление проиисходит обычно следующим образом:
        1)Блок который мы вернем пользователю он наинается с указателя на header найденного блока
        2)Указатель на новый блок, который останется во фрилисте получается как указатель на начало
        блока который изначаьно был найден во фрилисте, но слишком большого размера + размер запрошенного пользователем блока
*/
void splice_the_block(void* ptr,int size_of_found_free_block,int requested_size_of_block){
    void* rest_free_block_ptr = ptr + requested_size_of_block;
    int rest_free_block_size = size_of_found_free_block - requested_size_of_block;
    if(rest_free_block_size<MIN_SOTERED_SIZE){
        return;
    }
    //Рассматриваем ту часть, которая относится к запрашиваемому блоку и устанавливаем ей запрошенный размер
    ((metadata_t*)ptr)->size = requested_size_of_block;
    //Далее формируем блок который останется во фрилисте
    metadata_t header_of_new_block = {VACANT,rest_free_block_size};
    metadata_end_t footer_of_new_block = {VACANT,rest_free_block_size};
    metadata_t* rest_free_block_header_ptr = (metadata_t*)rest_free_block_ptr;
    metadata_end_t* rest_free_block_footer_ptr = (metadata_end_t*)get_ptr_to_footer(ptr);

    *rest_free_block_header_ptr = header_of_new_block;
    *rest_free_block_footer_ptr = footer_of_new_block;

    add_to_free_list(rest_free_block_ptr);

}
void* custom_malloc(int size){
    if(size<=0){
        //не используем printf тк он использует malloc, а это нам не надо
        write(STDOUT_FILENO, "Eror of custom_malloc ---> implicit size", strlen("Eror of custom_malloc ---> implicit size")); 
        return NULL;
    }
    //Теперь вспомним: помимо полезной нагрузки мы должны хранить header + footer, а еще блок не должен быть меньше, чем MIN_STORED_SIZE
    int real_allocating_size = size + HEADER_SZ + FOOTER_SZ;
    //Пытаемся найти во фрилисте уже выделенный, но свободный блок
    void* ptr = search_for_freeblock(real_allocating_size);
    //Если блок нашелся
    if(ptr){
        //Поскольку мы достаем блок из фрилиста, надо его пометить, как занятый
        set_tag_of_access(ptr,IN_USE);
        //Далее попробуем поделить найденный блок на 2, чтобы сэкономить память
        splice_the_block(ptr,((metadata_t*)ptr)->size,size);
        //Ну и поскольку пользователю надо вернуть указатель на полезную нагрузку, а не наши header или footer,то достаем указатель на payload
        return(get_ptr_to_payload(ptr));
    }
    //Если блока не нашлось
    /*
        Далее, чтобы сократить количество вызовов процессорных инструкций, таких как mmap допустим будем
        выделять памяти каждый раз в 2 раза больше (это нам ничем не повредить, потому что если что
        у нас есит split функция, которая если что поделит этот блок и выделит памяти просто без вызова 
        mmap, все равно эта память нам нужна будет)
    */
   int bytes_for_allocation = MAX(PAGES(real_allocating_size),last_size_of_mmaping)*PAGE_SIZE;
   last_size_of_mmaping*=2;
   void* new_block_of_memory = mmap(last_addres, bytes_for_allocation, PROT_READ | PROT_WRITE,MAP_ANONYMOUS| MAP_PRIVATE, -1, 0);
   if(new_block_of_memory == MAP_FAILED){
        return NULL;
   }
   //Делаем header/footer для нового блока
   metadata_t header = {IN_USE,bytes_for_allocation};
   metadata_t* header_ptr = (metadata_t*)new_block_of_memory;
   *header_ptr = header;
   metadata_end_t footer = {};
   footer.if_available = IN_USE;
   metadata_end_t* footer_ptr = (metadata_end_t*)get_ptr_to_footer(new_block_of_memory);
   *footer_ptr = footer;
   //далее поскольку мы выделили памяти чуть боьльше чем надо, засплитим полученный блок памяти и вернем указатель на него уже для пользователя
   splice_the_block(new_block_of_memory,bytes_for_allocation,real_allocating_size);
   //сдвинем адрес последнего блока
   last_addres = new_block_of_memory + bytes_for_allocation;
   
   printf("\n%ld,%ld,%ld,%d",((metadata_t*)new_block_of_memory)->size,HEADER_SZ,FOOTER_SZ,size);
   write(STDOUT_FILENO, "Succesfully allocated", strlen("Succesfully allocated:"));
   return(get_ptr_to_payload(new_block_of_memory));

}
/*
    При освобождении памяти нет смысла перефрагментировать нашу память, можно сделать функцию, которая
    будет обьединять освобожденные блоки,точнее будем смотреть, допустим, вот наша память:

    |занятый блок (1)|занятый блок, который собираемся отчищать (2)|свободный блок (3)|
    
    Поскольку выше существует функция splice_the_block и ,учитывая то, что чем более фрагментирована память,
    тем хуже, мы будем стараться добавлять во фрилист все блоки максимальной величины, но в памяти, 
    может выйти ситуация представленная выше, если просто освободить средний блок, то он полетит во фрилист,
    то есть туда добавится один лишний фрагмент, хотя можно было бы сделать так: вытащить блок (3) из фрилиста,
    освободить блок (2), соеденить блоки (2) и (3) и добавить получившееся во фрилист, если что при дальнейшей
    работе эти блоки подробятся, но так при каждом высвобождении памяти у нас фрагментация памяти не будет расти

*/
void stick_blocks(void* ptr){
    if(freelist_first_item!=NULL){
        metadata_t* header_of_block = (metadata_t*)ptr;
        metadata_end_t* footer_of_block = (metadata_end_t*)get_ptr_to_footer(ptr);
        //(metadata_end_t*)(ptr - FOOTER_SZ) - указатель на футер предыдущего блока памяти (т.е сейчас проверяем свободен ли предыдущий блок)
        if((metadata_end_t*)(ptr - FOOTER_SZ)!=NULL && ((metadata_end_t*)(ptr - FOOTER_SZ))->if_available==VACANT){
            void* prev_block_ptr = ptr -((metadata_end_t*)(ptr - FOOTER_SZ))->size;
            metadata_end_t* prev_block_footer_ptr = get_ptr_to_footer(prev_block_ptr);
            metadata_t* prev_block_header_ptr = (metadata_t*)prev_block_ptr;
            if(prev_block_ptr){
                //delete_from_free_list(header_of_block);
                //новый хедер - это хедер предыдущего блока, он же справа, поэтому просто сдвигаем хедер текущего блока в хедер предыдущего
                prev_block_header_ptr->size += header_of_block->size;
                //наш блок был справа, справа и останется, поэтому футер не меняем, меняем в нем только размер
                footer_of_block->size =  prev_block_header_ptr->size;
                //Сместили хедер текущего блока в хедер предыдущего,а футер оставили тем самым как бы получили огромный блок,слив их воедино
                header_of_block = prev_block_header_ptr;
                add_to_free_list(header_of_block);
            }
        }
        /*
            Далее тут есть такая штука, что свободный блок может располагаться в памяти и справа от рассматриваемого
            ,а может выйти так, что свободные блоки и слева, и справа, поэтому тут не else if, а еще один if
            Теперь смотрим свободен ли следующий блок и если что обьединяем (следующий по отношению к переданному ptr)
        */
        if(ptr+((metadata_t*)ptr)->size!=NULL && ((metadata_t*)(ptr+((metadata_t*)ptr)->size))->if_available==VACANT){
            void* next_block_ptr = ptr+((metadata_t*)ptr)->size;
            metadata_t* next_block_header_ptr = (metadata_t*)next_block_ptr;
            metadata_end_t* next_block_footer_ptr = get_ptr_to_footer(next_block_ptr);
            /*
                Тут ситуация такова |(1) блок|(2) блок|(3) блок|
                Если сместить хедер 2 блока вправо, то у меня останется блок памяти которая будет вообще подвешена
                и не понятно куда ушла (именно поэтому мы смещали в предыдущем ифе именно хедер рассматриваемого блока влево, а не наоборот)
                поэтому мы скажем, что хедер останется хедером,а вот футер второго станет новым -----> переместится на место футера 3го блока
            */
            //delete_from_free_list(header_of_block);
            header_of_block->size+=next_block_header_ptr->size;
            next_block_footer_ptr->size = header_of_block->size;
            footer_of_block = next_block_footer_ptr;
            add_to_free_list(header_of_block);
        }
    }
}
/*
    Далее тут все сложно нужна функция, которая будет очищать виртуальную память, выделенную mmap-ом
*/
int unmap(void* ptr,int size){
    //чистм size байтов
    //Тут надо просто проверить последний ли это блок, выделенной mmap-ом
    if(last_addres == ptr){
        last_addres = ptr - size;
    }
    return (munmap(ptr,(size_t)size));
}
void custom_free(void* payload_ptr){
    if(!payload_ptr){
        return;
    }
    void* block_ptr = payload_ptr - HEADER_SZ;
    /*
        Нужно проверить, а не свободен ли переданный нам блок, если он свободен, то нет нужды его освобождать
    */
   int size = ((metadata_t*)block_ptr)->size;
   metadata_t* block_header_ptr = (metadata_t*)block_ptr;
   uintptr_t adress = (uintptr_t)block_header_ptr;
   if(size%PAGE_SIZE == 0 && adress%PAGE_SIZE==0){
        //Если все пространство (PAGE) свободно или так скажем выровнено , мы может применять munmap
        unmap(block_ptr,size);
   }
   else{
        //склеим блоки а потом полученный блок добавим во фрилист, склеивать будем, если фрилист не пуст
        add_to_free_list(block_ptr);
        stick_blocks(block_ptr);
        //остаток вычистим
        if (size >= PAGE_SIZE && adress % PAGE_SIZE == 0) {
            splice_the_block(block_ptr, size, (size / PAGE_SIZE) * PAGE_SIZE);
            unmap(block_ptr, (size / PAGE_SIZE) * PAGE_SIZE);
        }
   }
   printf("%s","Free done");

}   
int main (void){
    metadata_t a;
    metadata_end_t b;
    //printf("%lu,%lu",sizeof(a),sizeof(b));
    char* ptr = custom_malloc(128);
    char abs[128]= "hello";
    int sdvig = strlen(abs);
    printf("\n%ld",sizeof(abs));
    custom_free(ptr);
    /*
    for(int i =0;i<=100;i++){
        memcpy(ptr+sdvig*i,abs,strlen(abs));
        printf("%s\n",ptr);
    }s
    //int* temp;
    //scanf("%d",temp);
    printf("\n%s\n",ptr);
    */
}
