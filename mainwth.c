#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h> 
#include <sys/stat.h>

///////////////////////////////////////////////////////////////////////////////
// constants
#define NORMAL_COLOR  "\x1B[0m"
#define GREEN  "\x1B[32m"
#define BLUE  "\x1B[34m"

#define MAX_WORD_LEN 64 //najveca dozvoljena duzina reci, uz \0
#define LETTERS 26 //broj slova u abecedi i broj dece u trie
#define SCANNED_FILES_SZ 100
///////////////////////////////////////////////////////////////////////////////
// structures
typedef struct scanned_file //datoteka koju je scanner vec skenirao
{
    char file_name[256]; //naziv datoteke
    time_t mod_time; //vreme poslednje modifikacije datoteke
} scanned_file;

typedef struct trie_node //cvor unutar trie strukture
{
    char c; //slovo ovog cvora
    int term; //flag za kraj reci
    int subwords; //broj reci u podstablu, ne racunajuci sebe
    struct trie_node *parent; //pokazivac ka roditelju
    struct trie_node *children[LETTERS]; //deca
} trie_node;

typedef struct search_result //rezultat pretrage
{
    int result_count; //duzina niza
    char **words; //niz stringova, svaki string duzine MAX_WORD_LEN
} search_result;
////////////////////////////////////////////////////////////////////////////////
// global variables

int shouldPrint = 1;
int cnt = 0;
trie_node *head = NULL;
char global_prefix[MAX_WORD_LEN] = {0};
pthread_mutex_t letterMutex[LETTERS];
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// search_result functions 
search_result* create_search_result(int size)
{
    search_result* new = (search_result*)malloc(sizeof(search_result));
    new->result_count = size;
    new->words = (char **)malloc(size*sizeof(char *));
    return new;
}

void trie_free_result(search_result *result)
{
    for(int i = 0; i < result->result_count; i++)
        free(result->words[i]);
    free(result->words);
    free(result);
}
///////////////////////////////////////////////////////////////////////////////
// Trie functions

trie_node* create_trie_node(char a, trie_node* parent, int term)
{
    trie_node* new = (trie_node *)malloc(sizeof(trie_node));
    new->c = a;
    new->term = term;
    new->subwords = 0;
    new->parent = parent;
    for(int i = 0; i < LETTERS; i++)
        new->children[i] = NULL;

    return new;
}

void init_trie()
{
    head = create_trie_node(-1, NULL, 0); 
    
    for(int i = 0; i < LETTERS; i++){
        head->children[i] = create_trie_node(i+97,head, 0);
        pthread_mutex_init(&letterMutex[i], NULL);
    }
    
}

// returns weather we added word or it had already existed
int add_trie_word(trie_node* parent, char* word)
{
    int term = 1;
    int should_add = 1;
    int slovo = 0;
    if(strlen(word) > 1)
        term = 0;

    for(int i = 0; i < LETTERS; i++){
        if(word[0] == i + 97){
            if(parent == head){
                slovo = i;
                pthread_mutex_lock(&letterMutex[i]); // locking first letter until everything else is done
            }
            if(parent->children[i] == NULL)
                parent->children[i] = create_trie_node(word[0], parent, 0);
            if(term){
                if(parent->children[i]->term){    
                    if(parent == head)  // in case first letter is regarded as a word, bu already added
                        pthread_mutex_unlock(&letterMutex[slovo]);
                    return 0;
                }
                if(parent == head)
                    parent->subwords++;
                parent->children[i]->term = term;
                if(parent == head)  // in case first letter is regarded as a word
                     pthread_mutex_unlock(&letterMutex[slovo]);
                return 1;
            }
            should_add = add_trie_word(parent->children[i], word+1);
            if(should_add)
                parent->children[i]->subwords++;
        }

    }

    if(should_add && parent == head)
        parent->subwords++;

    if(parent == head)  // unlocking first letter when we added a word 
        pthread_mutex_unlock(&letterMutex[slovo]);  
    return should_add;
}

void traverse_trie(trie_node* parent, char* word, int ii, int offset, search_result* src)
{
    if(parent == NULL)
        return;
    
    if(parent != head)
        word[ii++] = parent->c;
    if(parent->term){
        word[ii] = 0;
        for(int i = 0; i < strlen(word); i++)
            src->words[cnt][offset + i] = word[i];
        cnt++;
    }
    
    for(int l = 0; l < LETTERS; l++){
        if(parent->children[l] != NULL)
            traverse_trie(parent->children[l], word, ii, offset, src);
    }
        
}


// global cnt should set to 0 after every search
search_result* trie_get_words(trie_node* parent, char *prefix, char* pref, search_result* sr)
{
    int slovo = 0;
    for(int i = 0; i < LETTERS; i++){
        if(prefix[0] == i + 97){
            if(parent == head){
                slovo = i;
                pthread_mutex_lock(&letterMutex[slovo]);
            }
            if(parent->children[i] == NULL || (parent->children[i]->subwords == 0 && parent->children[i]->term == 0)){
                if(parent == head)
                    pthread_mutex_unlock(&letterMutex[slovo]);
                return NULL;
            }
            if(strlen(prefix) > 1)
                sr = trie_get_words(parent->children[i], prefix+1, pref, sr);
            else{
                int edge = 0;
                if(parent->children[i]->term)
                    edge = 1;
                sr = create_search_result(parent->children[i]->subwords + edge);
                for(int k = 0; k < parent->children[i]->subwords + edge; k++){
                    sr->words[k] = malloc(64);
                    for(int m = 0; m < strlen(pref)-1; m++)
                        sr->words[k][m] = pref[m];
                }
                char wrd[64] = {0};
                traverse_trie(parent->children[i], wrd, 0, strlen(pref)-1, sr);                
            }
        }
    }

    if(parent == head)
        pthread_mutex_unlock(&letterMutex[slovo]);

    return sr;
}

///////////////////////////////////////////////////////////////////////////////
// scanner thread

void read_data(char* filename, int size)
{
    FILE *fp = fopen(filename, "r");

    if (NULL == fp) {
        printf("%s file can't be opened \n", filename);
    }

    char * data = (char *) malloc(size*sizeof(char));
    char * delimiters = " \n\t";
    while (fgets(data, size, fp) != NULL) {
        char *token = strtok(data, delimiters);
        char word[64];
        while (token != NULL)
        {   
            
            strncpy(word, token, MAX_WORD_LEN);
            word[MAX_WORD_LEN] = 0;
            //////////////////////////////
            // checker
            int add = 1;
            for(int i = 0; i < strlen(word); i++){
                if((word[i] >= 'a' && word[i] <= 'z') || (word[i] >= 'A' && word[i] <= 'Z')){
                    if((word[i] >= 'A' && word[i] <= 'Z')){
                        word[i] = word[i] + 32;
                    }
                }else{
                    add = 0;
                    break;
                }
            }
            //modify add only if word is added add = 1
            //////////////////////////////
            // adding
            if(add)
                add = add_trie_word(head, word);    // return whether we added the word or not
            if(add){
                int isPref = strlen(global_prefix);
                for(int i = 0; i < isPref; i++){
                    if(global_prefix[i] != word[i]){
                        isPref = 0;
                        break;
                    }
                }
                if(shouldPrint && isPref!= 0)
                    printf("%s\n", word);
                // printf("[%s] shouldPrint: %d, prefix: %s - %d \n", word, shouldPrint, global_prefix, isPref);
            }
            //////////////////////////////
            
            token = strtok(NULL, delimiters);
        }
    }

    fclose(fp);

}

void* scanner(void* args)
{
    char *path = malloc(256);
    strcpy(path, (char *)args);
 
    scanned_file sc_files[100];
    memset(sc_files, 0, sizeof(sc_files));
    int counter = 0;
    while(1){
        DIR * dir = opendir(path); // open the path
        if(dir==NULL) return; // if was not able, return
        struct dirent * file; // for the directory entries
        while ((file = readdir(dir)) != NULL){ // if we were able to read something from the directory
            if(file-> d_type != DT_DIR){
                struct stat st;

                char filename[256] = {0};
                
                strcpy(filename, path);
                strcat(filename, "/");
                strcat(filename, file->d_name);
                stat(filename, &st);
                int size = st.st_size;
                time_t mod_time = st.st_mtime;
                // printf("%s%s -> %s   size: %d%s\n",BLUE,file->d_name, filename, size, NORMAL_COLOR);
                int should_read = 0;
                int i = 0;
                for(i = 0; i < SCANNED_FILES_SZ; i++){
                    if(strcmp(filename, sc_files[i].file_name) == 0){
                        if(mod_time != sc_files[i].mod_time){
                            should_read = 1;    // just read
                            sc_files[i].mod_time = mod_time;
                            printf("Modified old: %s%s -> %s   size: %d%s\n",BLUE,file->d_name, filename, size, NORMAL_COLOR);
                        }
                        break;
                    }
                }
                if(i == SCANNED_FILES_SZ)
                    should_read = 2;    // add + read
                if(should_read){
                    read_data(filename, size);
                    if(should_read == 2){
                        if(counter < SCANNED_FILES_SZ){
                            scanned_file new_scanned;
                            strcpy(new_scanned.file_name, filename);
                            new_scanned.mod_time = mod_time;
                            sc_files[counter++] = new_scanned;
                            printf("Added new: %s%s -> %s   size: %d%s\n",BLUE,file->d_name, filename, size, NORMAL_COLOR);
                            // printf("Name: %s%s%s\n", GREEN, sc_files[counter-1].file_name,NORMAL_COLOR);
                        }else
                            printf("Can`t share more than %d files in folder %s\n", SCANNED_FILES_SZ, path);
                    }
                }
                
            }
        }

        // wait for 5 seconds and read again
        // printf("folder %s has %d files in it\n", path, counter);
        sleep(5);
        closedir(dir); // finally close the directory
    }
    
}
///////////////////////////////////////////////////////////////////////////////
// com thread
void* com(void *args)
{
    int n = 1;
    int take_com = 1; 
    pthread_t threads[100];
    int curr = 0;
    while(1){
            char command[256] = {0};
            n = scanf("%s", command);            
            if(take_com && n == 1){
                    // printf("%s, n = %d\n", command, n);
                    if(strcmp(command, "_stop_") == 0){
                        for(int i = 0; i < LETTERS; i++)
                            pthread_mutex_destroy(&letterMutex[i]);
                        return;
                    }
                    if(strncmp(command, "_add_", 5) == 0){  // _add_dir [without space between names]
                        char dir[256] = {0};
                        int j = 0;
                        for(int i = 5; i < strlen(command);i++)
                            dir[j++] = command[i];
                        dir[j] = 0;
                        pthread_create(&threads[curr], NULL, scanner, (void*)dir);
                        curr++;
                    }else{
                        global_prefix[0] = 0;
                        take_com = 0;
                        shouldPrint = 1;
                        strcpy(global_prefix, command);
                        /////////////////////////////////////
                        //search result part
                        printf("searching for prefix -> %s\n", command);
                        search_result* sr = NULL;
                        sr = trie_get_words(head, global_prefix, global_prefix, sr);
                        if(sr == NULL){
                            printf("no words with prefix %s yet.\n", global_prefix);
                        }else{
                            for(int i = 0; i < sr->result_count; i++)
                                printf("%s\n", sr->words[i]);
                            cnt = 0;
                            trie_free_result(sr);
                        }
                        ////////////////////////////////////
                    }

            }
            if(n == EOF){
                global_prefix[0]=0;
                shouldPrint = 0;
                clearerr(stdin);
                take_com = 1;
            }
    }
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// main
int main()
{   
    init_trie();
    pthread_t t2;
    pthread_create(&t2, NULL, com, NULL);
    pthread_join(t2, NULL);
    return 0;
}