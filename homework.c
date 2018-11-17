/*
 * file:        homework.c
 * description: skeleton file for CS 7600 homework 3
 *
 * CS 7600, Intensive Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2015
 */

#define FUSE_USE_VERSION 27
#define LRU_DIR_CACHE_SIZE 50
#define DIRTY_CACHE_SIZE 10
#define CLEAN_CACHE_SIZE 30

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "fs7600.h"
#include "blkdev.h"


extern int homework_part;       /* set by '-part n' command-line option */

/* 
 * disk access - the global variable 'disk' points to a blkdev
 * structure which has been initialized to access the image file.
 *
 * NOTE - blkdev access is in terms of 1024-byte blocks
 */
extern struct blkdev *disk;

// cache structs.
struct cache_item{
    uint32_t par_inode;
    char file_name[28];
    uint32_t child_inode;
    uint8_t child_is_dir;
    uint8_t is_valid;
    time_t entry_time;
};

struct write_back_cache_item{
    uint32_t blk_addr;
    uint8_t is_valid;
    time_t entry_time;
    char data[FS_BLOCK_SIZE];
};

struct cache_item dir_entry_cache[LRU_DIR_CACHE_SIZE];
struct write_back_cache_item clean_write_cache[CLEAN_CACHE_SIZE];
struct write_back_cache_item dirty_write_cache[DIRTY_CACHE_SIZE];

/*
 * cache - you'll need to create a blkdev which "wraps" this one
 * and performs LRU caching with write-back.
 */
int cache_nops(struct blkdev *dev) 
{
    struct blkdev *d = dev->private;
    return d->ops->num_blocks(d);
}

// search the thing to see if it's found already.
int search_write_back_cache(struct write_back_cache_item* write_cache, uint32_t cache_size, uint32_t block_address){
    // return the item if found in cache.
    uint32_t i;
    for (i = 0; i < cache_size; i++){
        if(write_cache[i].is_valid == 1 && write_cache[i].blk_addr == block_address){
            return i;
        }
    }
    return -1;
}

// get least recently used entry.
int get_write_back_cache_lru_entry(struct write_back_cache_item* write_cache, uint32_t cache_size){
    // return the item if found in cache.
    uint32_t i;
    time_t lru_time = (time_t) time(NULL);
    int32_t lru_idx = -1;
    for (i = 0; i < cache_size; i++){
        if(write_cache[i].is_valid == 0){
            return i;
        }
        // least recently used index.        
        if (write_cache[i].is_valid == 1 && lru_time >= write_cache[i].entry_time){
            lru_idx = i;
            lru_time = write_cache[i].entry_time;
        }
    }
    return lru_idx;
}

// move an entry from clean to dirty.
int write_back_cache_clean_to_dirty_entry(uint32_t clean_idx, uint32_t dirty_idx){
    clean_write_cache[clean_idx].is_valid = 0;
    dirty_write_cache[dirty_idx].is_valid = 1;
    dirty_write_cache[dirty_idx].blk_addr = clean_write_cache[clean_idx].blk_addr;
    dirty_write_cache[dirty_idx].entry_time = (time_t) time(NULL);
    return 0;
}

int write_back_cache_flush_dirty_entry(struct blkdev *dev, uint32_t dirty_idx){
    //dirty_write_cache[dirty_idx].is_valid = 0;
    struct blkdev *disk = dev->private;
    uint32_t block_address = dirty_write_cache[dirty_idx].blk_addr;
    char * disk_write_data = dirty_write_cache[dirty_idx].data;
    if (dirty_write_cache[dirty_idx].is_valid == 1) {
       if(disk->ops->write(disk, block_address, 1, disk_write_data) < 0)
           exit(1);
       dirty_write_cache[dirty_idx].is_valid = 0;
    }
    return dirty_idx;
}



int cache_read_for_one(struct blkdev *dev, uint32_t block_address, uint32_t idx, void *buf, int first, int n)
{
    char* write_buffer = (char*) buf;
    // look in the dirty cache.
    int dirty_cache_idx = search_write_back_cache(dirty_write_cache, DIRTY_CACHE_SIZE, block_address);
    // if the page is found in the cache return the page.
    if (dirty_cache_idx >= 0){
        dirty_write_cache[dirty_cache_idx].entry_time = time(NULL);
        memcpy(&write_buffer[idx*FS_BLOCK_SIZE], dirty_write_cache[dirty_cache_idx].data, FS_BLOCK_SIZE);
        return SUCCESS;
    }
    struct blkdev *disk = dev->private;
    // look at the clean cache.
    int clean_cache_idx = search_write_back_cache(clean_write_cache, CLEAN_CACHE_SIZE, block_address);
    // if found then okay.
    if (clean_cache_idx >= 0){
        clean_write_cache[clean_cache_idx].entry_time = time(NULL);
    }
    else{
        //  otherwise evict a page and copy it, then return.
        clean_cache_idx = get_write_back_cache_lru_entry(clean_write_cache, CLEAN_CACHE_SIZE);
        // expell this entry and update it.
        clean_write_cache[clean_cache_idx].is_valid = 1;
        clean_write_cache[clean_cache_idx].entry_time = time(NULL);
        clean_write_cache[clean_cache_idx].blk_addr = block_address;
        // read the data from the disk.
        char * disk_read_data = clean_write_cache[clean_cache_idx].data;
        if(disk->ops->read(disk, block_address, 1, disk_read_data) < 0)
           exit(1);        
    }
    // now return the memory.
    memcpy(&write_buffer[idx*FS_BLOCK_SIZE], clean_write_cache[clean_cache_idx].data, FS_BLOCK_SIZE);
    return SUCCESS;
}

int cache_write_for_one(struct blkdev *dev, uint32_t block_address, uint32_t idx, void *buf, int first, int n)
{
    char* write_buffer = (char*) buf;
    //look through dirty cache. If block is found, update it and return
    // look in the dirty cache.
    int dirty_cache_idx = search_write_back_cache(dirty_write_cache, DIRTY_CACHE_SIZE, block_address);
    // if the page is found in the cache return the page.
    if (dirty_cache_idx >= 0){
        dirty_write_cache[dirty_cache_idx].entry_time = time(NULL);
        memcpy(dirty_write_cache[dirty_cache_idx].data, &write_buffer[idx*FS_BLOCK_SIZE], FS_BLOCK_SIZE);
        return SUCCESS;
    }
    // get a dirty entry and write this to disk and invalidate it.
    dirty_cache_idx = get_write_back_cache_lru_entry(dirty_write_cache, DIRTY_CACHE_SIZE);
    write_back_cache_flush_dirty_entry(dev, dirty_cache_idx);

    struct blkdev *disk = dev->private;
    // look at the clean cache.
    int clean_cache_idx = search_write_back_cache(clean_write_cache, CLEAN_CACHE_SIZE, block_address);
    // if found then okay.
    //look through clean cache. If block is found, remove it, update it, and put it in dirty cache
    if (clean_cache_idx >= 0){
        write_back_cache_clean_to_dirty_entry(clean_cache_idx, dirty_cache_idx);        
        memcpy(dirty_write_cache[dirty_cache_idx].data, &write_buffer[idx*FS_BLOCK_SIZE], FS_BLOCK_SIZE);
        return SUCCESS;        
    }
    //choose the LRU block from the dirty cache, write it back, and use its space to hold the new block
    dirty_write_cache[dirty_cache_idx].is_valid = 1;
    dirty_write_cache[dirty_cache_idx].blk_addr = block_address;
    dirty_write_cache[dirty_cache_idx].entry_time = (time_t) time(NULL);    
    memcpy(dirty_write_cache[dirty_cache_idx].data, &write_buffer[idx*FS_BLOCK_SIZE], FS_BLOCK_SIZE);
    return SUCCESS;        
    
 }

int cache_read(struct blkdev *dev, int first, int n, void *buf)
{
    uint32_t block_address;
    uint32_t idx;
    for (idx = 0; idx < n; idx++){
        block_address = first + idx;
        cache_read_for_one(dev, block_address, idx, buf, first, n);
    } 
    return SUCCESS;
}

int cache_write(struct blkdev *dev, int first, int n, void *buf)
{
    uint32_t block_address;
    uint32_t idx;
    for (idx = 0; idx < n; idx++){
        block_address = first + idx;
        cache_write_for_one(dev, block_address, idx, buf, first, n);
    } 
    return SUCCESS;
}
struct blkdev_ops cache_ops = {
    .num_blocks = cache_nops,
    .read = cache_read,
    .write = cache_write
};
struct blkdev *cache_create(struct blkdev *d)
{
    struct blkdev *dev = malloc(sizeof(*d));
    dev->ops = &cache_ops;
    dev->private = d;
    return dev;
}
/*
 * A structure to store strat positions of the blocks
 * of the bitmaps.
 */
struct superblock_memory
{
    uint32_t superblock_size;
    uint32_t magic;
    uint32_t root_inode; //start of inodes
    uint32_t inode_map_start;
    uint32_t inode_map_size;
    uint32_t inode_region_size;
    uint32_t inode_region_start;
    uint32_t block_map_start;
    uint32_t block_map_size;
    uint32_t block_region_start;
    uint32_t num_blocks;
    uint16_t ibitmap_modified;
    uint16_t bbitmap_modified;
    uint32_t num_inodes_tot;
};

/*
 * Structure containing information of inodes of a given path.
 */
struct path_info
{
    uint32_t cur_inode;
    uint32_t par_inode;
    char file_name[28];
    uint32_t is_dir;
    uint32_t par_is_dir;
};

struct file_dir_handler
{
    uint32_t f_cur_inode;
    uint32_t f_par_inode;
    uint32_t f_is_dir;
    struct fs7600_dirent *f_dir_entry;
};

struct file_temp_info
{
    uint32_t indir_1_modified;
    uint32_t indir_2_modified;
    uint32_t indir_1_read_from_disk;
    uint32_t indir_2_read_from_disk;
};

/* by defining bitmaps as 'fd_set' pointers, you can use existing
 * macros to handle them. 
 *   FD_ISSET(##, inode_map);
 *   FD_CLR(##, block_map);
 *   FD_SET(##, block_map);
 */
fd_set *inode_map;              /* = malloc(sb.inode_map_size * FS_BLOCK_SIZE); */
fd_set *block_map;
struct superblock_memory sb_memory;


/*
 * List of all the inodes stored on the disk.
 */
struct fs7600_inode *inode_list;

/* init - this is called once by the FUSE framework at startup. Ignore
 * the 'conn' argument.
 * recommended actions:
 *   - read superblock
 *   - allocate memory, read bitmaps and inodes
 */
void* fs_init(struct fuse_conn_info *conn)
{
    printf("Initialization: It begins.");

    struct fs7600_super sb;
    if (disk->ops->read(disk, 0, 1, &sb) < 0)
        exit(1);

    if (homework_part > 3)
        disk = cache_create(disk);

    printf("Initialization: Our code begins.");

    /* your code here */

    // Initialize our superblock-structure.
    sb_memory.superblock_size = 1;
    sb_memory.magic = sb.magic;
    sb_memory.root_inode = sb.root_inode;
    sb_memory.inode_map_start = sb_memory.superblock_size + 0;
    sb_memory.inode_map_size = sb.inode_map_sz;
    sb_memory.inode_region_size = sb.inode_region_sz;
    sb_memory.block_map_start = sb_memory.inode_map_start + sb_memory.inode_map_size;
    sb_memory.block_map_size = sb.block_map_sz;
    sb_memory.num_blocks = sb.num_blocks;
    sb_memory.inode_region_start = sb_memory.block_map_start + sb_memory.block_map_size; 
    sb_memory.block_region_start = sb_memory.inode_region_start + sb.inode_region_sz;
    sb_memory.ibitmap_modified = 0;
    sb_memory.bbitmap_modified = 0;
    sb_memory.num_inodes_tot = (sb.inode_region_sz * FS_BLOCK_SIZE)/sizeof(struct fs7600_inode); // aka div by 64.    
    printf("Initialization: Memory-allocation begins.");

    // Allocate memory.
    inode_map = malloc(sb.inode_map_sz * FS_BLOCK_SIZE);
    block_map = malloc(sb.block_map_sz * FS_BLOCK_SIZE);
    inode_list = malloc(sb.inode_region_sz * FS_BLOCK_SIZE);
    
    printf("Initialization: Bitmaps-reads begins.");

    // Read bitmaps. 
    if (disk->ops->read(disk, sb_memory.inode_map_start, sb_memory.inode_map_size, inode_map) < 0)
        exit(1);

    if (disk->ops->read(disk, sb_memory.block_map_start, sb_memory.block_map_size, block_map) < 0)
        exit(1);

    printf("Initialization: Inodes-reads begins.");
    
    // Read inodes.
    if (disk->ops->read(disk, sb_memory.inode_region_start,
                sb_memory.inode_region_size, inode_list) < 0)
        exit(1);

    printf("Initialization: It ends.");

    // set all cache entries to invalid.
    uint32_t i;
    for (i=0; i<LRU_DIR_CACHE_SIZE; i++){
        dir_entry_cache[i].is_valid = 0;
    }
    for (i=0; i<CLEAN_CACHE_SIZE; i++){
        clean_write_cache[i].is_valid = 0;
    }
    for (i=0; i<DIRTY_CACHE_SIZE; i++){
        dirty_write_cache[i].is_valid = 0;
    }

    return NULL;
}

static char* get_parent_directory(const char *child_dir){
    char *path_copy = strdup(child_dir);
    if (path_copy[strlen(path_copy)-1] == '/' && strlen(path_copy) > 1){
        path_copy[strlen(path_copy)-1] = 0;
    }
    // now get the last '/'
    char *pch;
    pch = strrchr(path_copy, '/');
    uint32_t pos = pch - path_copy;
    // root is parent
    if (pos == 0){
        char *return_arr = malloc(2 * sizeof(char));
        return_arr[0] = '/';
        return_arr[1] = 0;
        free(path_copy);
        return return_arr;
    }
    // otherwise generic parent.
    char *return_arr = malloc((pos+1) * sizeof(char));
    uint32_t i;
    for(i=0; i < pos; i ++){
        return_arr[i] = path_copy[i];
    }
    return_arr[pos] = 0;
    free(path_copy);
    return return_arr;
}

static void write_inode_to_disk(uint32_t file_inode_number){
    size_t inode_size = sizeof(struct fs7600_inode);
    uint32_t num_inodes_per_blk = FS_BLOCK_SIZE / inode_size;
    uint32_t modified_blk_number = file_inode_number / num_inodes_per_blk;
    uint32_t write_position = sb_memory.inode_region_start + modified_blk_number;
    // get the location of inode at the start of this modified block
    struct fs7600_inode *modified_blk_start_inode = &inode_list[modified_blk_number * num_inodes_per_blk];
    // write block
    if(disk->ops->write(disk, write_position, 1, modified_blk_start_inode) < 0)
       exit(1);

}

static void write_blockbitmap_to_disk(){
    if(disk->ops->write(disk, sb_memory.block_map_start, sb_memory.block_map_size, block_map) < 0)
        exit(1);         

}

static void write_inodebitmap_to_disk(){
    if(disk->ops->write(disk, sb_memory.inode_map_start, sb_memory.inode_map_size, inode_map) < 0)
        exit(1); 
}



/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path is not present.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */

static int get_path(const char *path, struct path_info *pi)
{
    char *path_copy = strdup(path);
    char *parent_path;
    int parent_inode_number;
    int current_inode_number;

    // absolute path
    if (path_copy[0] == '/'){
        current_inode_number = sb_memory.root_inode;
        parent_inode_number = sb_memory.root_inode;
        pi->is_dir = 1;
    }
    
    char* dir_name = strtok(path_copy,"/");
    while (dir_name != NULL)
    {
        parent_inode_number = current_inode_number;
        strcpy(pi->file_name, dir_name);
        current_inode_number = get_inode_num(current_inode_number, dir_name, pi);
        if (current_inode_number < 0)
        {
            free(path_copy);
            return -ENOENT;
        }
        dir_name = strtok(NULL, "/");
    }

    pi->cur_inode = current_inode_number;
    pi->par_inode = parent_inode_number;

    free(path_copy);
    return 0;
}

int try_dir_entry_cache(int par_inode, const char *name, struct path_info *pi){
    uint32_t i;
    for (i=0; i<LRU_DIR_CACHE_SIZE; i++){
        if(dir_entry_cache[i].is_valid == 1 && dir_entry_cache[i].par_inode == par_inode && strcmp(dir_entry_cache[i].file_name, name) == 0){
            pi->cur_inode = dir_entry_cache[i].child_inode;
            pi->par_inode = dir_entry_cache[i].par_inode;
            pi->is_dir = dir_entry_cache[i].child_is_dir;
            dir_entry_cache[i].entry_time = (time_t) time(NULL); 
            return 0;
        }
    }
    return -ENOENT;
}

int add_to_dir_entry_cache(struct fs7600_dirent *dir_entry, int idx, uint32_t parent_inode_num){
    // cache is not full
    time_t lru_time = (time_t) time(NULL);
    int32_t lru_idx = -1;
    uint32_t i;
    for (i=0; i<LRU_DIR_CACHE_SIZE; i++){
        if(dir_entry_cache[i].is_valid == 0){
            dir_entry_cache[i].child_inode = dir_entry[idx].inode;
            dir_entry_cache[i].par_inode = parent_inode_num;
            dir_entry_cache[i].child_is_dir = dir_entry[idx].isDir;
            strcpy(dir_entry_cache[i].file_name, dir_entry[idx].name);
            dir_entry_cache[i].entry_time = (time_t) time(NULL); 
            dir_entry_cache[i].is_valid = 1;
            return 0;
        }
        // mark the least recently used entry.
        if (dir_entry_cache[i].is_valid == 1 && lru_time >= dir_entry_cache[i].entry_time){
            lru_idx = i;
            lru_time = dir_entry_cache[i].entry_time;
        }
    }
    // cache is full, so write to lru entry.
    dir_entry_cache[lru_idx].child_inode = dir_entry[idx].inode;
    dir_entry_cache[lru_idx].par_inode = parent_inode_num;
    dir_entry_cache[lru_idx].child_is_dir = dir_entry[idx].isDir;
    strcpy(dir_entry_cache[lru_idx].file_name, dir_entry[idx].name);
    dir_entry_cache[lru_idx].entry_time = (time_t) time(NULL); 
    dir_entry_cache[lru_idx].is_valid = 1;
    return 0;
}

int flush_dir_entry_cache_entry(int inode_number){
    uint32_t i;
    for (i=0; i<LRU_DIR_CACHE_SIZE; i++){
        if(dir_entry_cache[i].is_valid == 1 && dir_entry_cache[i].child_inode == inode_number){
            dir_entry_cache[i].is_valid = 0;
        }
    }    
    return 0;
}

int rename_dir_entry_cache_entry(int inode_number, char* new_name){
    uint32_t i;
    for (i=0; i<LRU_DIR_CACHE_SIZE; i++){
        if(dir_entry_cache[i].is_valid == 1 && dir_entry_cache[i].child_inode == inode_number){
            strcpy(dir_entry_cache[i].file_name, new_name);
            dir_entry_cache[i].is_valid = 1;
            dir_entry_cache[i].entry_time = (time_t) time(NULL); 
        }
    }    
    return 0;    
}


int get_inode_num(int cur_inode, const char *path, struct path_info *pi){
    // try lru cache
    if (homework_part > 2){
        if (try_dir_entry_cache(cur_inode, path, pi) >= 0){
            return pi->cur_inode;
        }
    }

    // no cache
    // Only one block in use for inode
    uint32_t inode_block_number = inode_list[cur_inode].direct[0];

    struct fs7600_dirent *dir_entry;
    dir_entry = malloc(FS_BLOCK_SIZE);
    // read the inode block
    if (disk->ops->read(disk,inode_block_number,1,dir_entry) < 0)
        exit(1);

    // iterate through the block and match the file name. one directory can only have 32 enteries
    uint32_t num_enteries = FS_BLOCK_SIZE / sizeof(struct fs7600_dirent); // this should always be 32
    
    uint32_t i;
    for (i=0; i < num_enteries; i++){
        if(dir_entry[i].valid == 1 && strcmp(dir_entry[i].name, path) == 0){
            pi->is_dir = dir_entry[i].isDir;
            int inode_return = dir_entry[i].inode;
            if (homework_part > 2){
                // add to the cache, since we didn't find it in the first place.
                add_to_dir_entry_cache(dir_entry, i, cur_inode);
            }
            free(dir_entry);
            return inode_return;
        }
    }
    free(dir_entry);
    return -1;
}

static int getattr_inode(int inode, struct stat *sb){

    if (inode == 0)
        return -ENOENT;

    memset(sb, 0, sizeof(sb));
    sb->st_mode = (mode_t) inode_list[inode].mode;
    sb->st_uid = (uid_t) inode_list[inode].uid;
    sb->st_gid = (gid_t) inode_list[inode].gid;
    sb->st_ctime = (time_t) inode_list[inode].ctime;
    sb->st_mtime = (time_t) inode_list[inode].mtime;
    sb->st_size = (off_t) inode_list[inode].size;

    return 0;

}

static int getattr_path_info(struct path_info *pi, struct stat *sb){
    return getattr_inode(pi->cur_inode, sb);
} 

/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - fields not provided in CS7600fs are:
 *    st_nlink - always set to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * errors - path translation, ENOENT
 */
static int fs_getattr(const char *path, struct stat *sb)
{
    // If sb is null or invalid,
    // see what error to return.
    // Else,
    // malloc and memset 0 to everything in the allocated space.
    // Need to check if path is null.
    // In that case we return something that says, "error."
    struct path_info pi;

    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }
    
    return getattr_inode(pi.cur_inode, sb);

}

/* readdir - get directory contents.
 *
 * for each entry in the directory, invoke the 'filler' function,
 * which is passed as a function pointer, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a struct stat, just like in getattr.
 *
 * Errors - path resolution, ENOTDIR, ENOENT
 */
static int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
    struct path_info pi;
    // get path
    struct fs7600_dirent *dir_entries;
    if (homework_part == 1){
        if (get_path(path, &pi) < 0) {
            return -ENOENT;
        }
        // see if it's a directory
        if (pi.is_dir == 0){
            return -ENOTDIR;
        }        
        dir_entries = malloc(FS_BLOCK_SIZE);
        // get block number and read the directory.
        uint32_t dir_inode_blk_num = inode_list[pi.cur_inode].direct[0];
         
        // read the inode block
        if (disk->ops->read(disk,dir_inode_blk_num,1,dir_entries) < 0)
            exit(1);

    }
    else{
        // this will always be a directory. 
        struct file_dir_handler *fi_dir_handler = (struct file_dir_handler *)(uint64_t)fi->fh;
        if (fi_dir_handler->f_is_dir == 1){
            dir_entries = fi_dir_handler->f_dir_entry;
        }
        pi.cur_inode = fi_dir_handler->f_cur_inode;
        pi.par_inode = fi_dir_handler->f_par_inode;
        pi.is_dir = fi_dir_handler->f_is_dir;
    }

    uint32_t num_enteries = FS_BLOCK_SIZE / sizeof(struct fs7600_dirent); // this should always be 32

    uint32_t i;
    for (i=0; i < num_enteries; i++){
        if(dir_entries[i].valid == 1){
            struct stat sb;
            if (getattr_inode(dir_entries[i].inode, &sb) >= 0){
                filler(ptr, dir_entries[i].name, &sb, 0);                            
            }
        }
    }
    if (homework_part == 1){
        free(dir_entries);
    }
    return 0;
}

/* see description of Part 2. In particular, you can save information 
 * in fi->fh. If you allocate memory, free it in fs_releasedir.
 */
static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
    if (homework_part == 1){
        return 0;    
    }
    struct path_info pi;
    memset(&pi, 0, sizeof(struct path_info));
    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }
    
    if (pi.is_dir == 0){
            return -ENOTDIR;
    }        

    struct file_dir_handler *fi_dir_handler;
    fi_dir_handler = malloc(sizeof(struct file_dir_handler));
    fi_dir_handler->f_cur_inode = pi.cur_inode;
    fi_dir_handler->f_par_inode = pi.par_inode;
    fi_dir_handler->f_is_dir = 1;
    fi_dir_handler->f_dir_entry = malloc(FS_BLOCK_SIZE);
    struct fs7600_dirent *temp = fi_dir_handler->f_dir_entry;
        
    // get block number and read the directory.
    uint32_t dir_inode_blk_num = inode_list[pi.cur_inode].direct[0];
     
    // read the inode block
    if (disk->ops->read(disk,dir_inode_blk_num,1, temp) < 0)
        exit(1);

    fi->fh = (uint64_t) fi_dir_handler;

    //fi->fh = pi.cur_inode;

    // read the dirent of this directory and store it in a variable(maybe global variable)

    return 0;
}

static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{    
    if (homework_part == 1){
        return 0;    
    }
    // release the dirent stuff and free the malloc.
    //free(fi_file_dir_entry);
    struct file_dir_handler *fi_dir_handler = (struct file_dir_handler *)(uint64_t)fi->fh;
    if (fi_dir_handler->f_is_dir == 1){
        struct fs7600_dirent *temp = fi_dir_handler->f_dir_entry;
        free(temp);        
    }
    free((struct file_dir_handler *)(uint64_t)fi->fh);
    return 0;
}

/* mknod - create a new file with permissions (mode & 01777)
 *
 * Errors - path resolution, EEXIST
 *          in particular, for mknod("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If this would result in >32 entries in a directory, return -ENOSPC
 * if !S_ISREG(mode) return -EINVAL [i.e. 'mode' specifies a device special
 * file or other non-file object]
 */

static int get_free_inode_number(){
    // get an empty block. leaving some blocks for stuff. iterate till the end of the disk
    uint32_t i;
    for (i=0; i < sb_memory.num_inodes_tot; i++){
        if(FD_ISSET(i, inode_map) == 0){
            FD_SET(i, inode_map);
            return i;
        }
    }    
    return -1;
}

static int set_inode_number_free(uint32_t inode_number){
    // get an empty block. leaving some blocks for stuff. iterate till the end of the disk
    if(FD_ISSET(inode_number, inode_map)){
        FD_CLR(inode_number, inode_map);
    }
    return 0;
}

static int get_free_block_number(){
    // get an empty block. leaving some blocks for stuff. iterate till the end of the disk
    uint32_t i;
    for (i=sb_memory.block_region_start; i < sb_memory.num_blocks; i++){
        if(FD_ISSET(i, block_map) == 0){
            FD_SET(i, block_map);
            return i;
        }
    }
    return -1;
}

static int set_block_number_free(uint32_t blk_address){
    // get an empty block. leaving some blocks for stuff. iterate till the end of the disk
    if(FD_ISSET(blk_address, block_map)){
        FD_CLR(blk_address, block_map);
    }
    return 0;
}

static int fs_mknod_mkdir(const char *path, mode_t mode, uint8_t is_mkdir){
    struct path_info pi;
    memset(&pi, 0, sizeof(struct path_info));
    struct path_info parent_pi;
    // path exists
    if (get_path(path, &pi) >= 0) {
        return -EEXIST;
    }

    // see if parent directory exists
    char* parent_dir_path = get_parent_directory(path);
    char *path_copy = strdup(parent_dir_path);
    free(parent_dir_path);

    if (get_path(path_copy, &parent_pi) < 0) {
        free(path_copy);
        return -ENOENT;
    }
    free(path_copy);

    // parent is not a directory.
    if (parent_pi.is_dir == 0){
        return -ENOTDIR;
    }

    uint32_t i;

    // get an empty inode.
    int free_inode = -1;
    free_inode = get_free_inode_number();
    if (free_inode == -1){
        return -ENOSPC;
    }

    int free_block = -1;
    if(is_mkdir == 1){ 
        free_block = get_free_block_number();
        if (free_block == -1){
            return -ENOSPC;
        }
    }

    uint32_t parent_dir_ptr = inode_list[parent_pi.cur_inode].direct[0];
    uint32_t parent_gid = inode_list[parent_pi.cur_inode].gid;
    uint32_t parent_uid = inode_list[parent_pi.cur_inode].uid;

    struct fs7600_dirent *dir_entry;
    dir_entry = malloc(FS_BLOCK_SIZE);
    // read the parent directory and see if 32 entries issue.
    if(disk->ops->read(disk, parent_dir_ptr, 1, dir_entry) < 0)
       exit(1);
    
    uint32_t num_enteries = FS_BLOCK_SIZE / sizeof(struct fs7600_dirent); // this should always be 32    
    // iterate through the enteries and see if all are valid. if all are valid then return error       
    int empty_entry = -1;
    for (i=0; i < num_enteries; i++){
        if(dir_entry[i].valid == 0){
            empty_entry = i;
            break;
        }
    }

    if (empty_entry == -1){
        free(dir_entry);
        return -ENOSPC;
    }


    // now use the location of empty entery as a new entery.
    dir_entry[empty_entry].valid = 1;
    if (is_mkdir == 1){
        dir_entry[empty_entry].isDir = 1;
    }
    else{
        dir_entry[empty_entry].isDir = 0;
    }
    strcpy(dir_entry[empty_entry].name, pi.file_name);

    // set direntry inode as the free inode; 
    dir_entry[empty_entry].inode = free_inode;

    // add the inode entry
    // add the inode entry
    struct fuse_context *fs_ctx = fuse_get_context();
    inode_list[free_inode].gid = fs_ctx->gid;
    inode_list[free_inode].uid = fs_ctx->uid;

    //inode_list[free_inode].gid = parent_gid; 
    //inode_list[free_inode].uid = parent_uid; 
    if (is_mkdir == 1){
       inode_list[free_inode].mode = S_IFDIR | mode; 
       inode_list[free_inode].direct[0] = free_block;
       //inode_list[free_inode].size = (size_t) FS_BLOCK_SIZE;                     
       inode_list[free_inode].size = (size_t) 0; // since pete's dir1 has size 0.                     
    }
    else{
       mode_t file_mode = mode & 01777;
       inode_list[free_inode].mode = file_mode | S_IFREG;         
       inode_list[free_inode].size = (size_t) 0;                     
       for (i=0;i<N_DIRECT;i++){
           inode_list[free_inode].direct[i] = 0;
       }
    }
    inode_list[free_inode].ctime = (time_t) time(NULL);
    inode_list[free_inode].mtime = (time_t) time(NULL); 
    inode_list[free_inode].indir_1 = 0;
    inode_list[free_inode].indir_2 = 0;

    // null initialize the directory(make all entries invalid.

    // write back direntry block of parent
    if(disk->ops->write(disk, parent_dir_ptr, 1, dir_entry) < 0)
       exit(1);

    // write the dirent of child
    if (is_mkdir == 1){
        struct fs7600_dirent dirent_new_dir[num_enteries];
        memset(dirent_new_dir, 0, FS_BLOCK_SIZE);
        if(disk->ops->write(disk, free_block, 1, dirent_new_dir) < 0)
           exit(1);        
    }

    // write inode list
    write_inode_to_disk(free_inode);

    // write inode bitmap
    write_inodebitmap_to_disk();
    
    // write block bitmap
    if (is_mkdir == 1){
        write_blockbitmap_to_disk();
    }
    

    free(dir_entry);
    return 0;
}


static int fs_mknod(const char *path, mode_t mode, dev_t dev)
{
    if (!S_ISREG(mode)){
        return -EINVAL;
    } 

    return fs_mknod_mkdir(path, mode,  0);
}

/* mkdir - create a directory with the given mode.
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create. 
 * If this would result in >32 entries in a directory, return -ENOSPC
 *
 * Note that you may want to combine the logic of fs_mknod and
 * fs_mkdir. 
 */ 
static int fs_mkdir(const char *path, mode_t mode)
{
    return fs_mknod_mkdir(path, mode,  1);
}

/* truncate - truncate file to exactly 'len' bytes
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */

static int truncate_inode(uint32_t file_inode_num){
    struct fs7600_inode *file_inode = &inode_list[file_inode_num];

    uint32_t num_blks_in_file = file_inode->size / FS_BLOCK_SIZE; // due to some extra bytes.
    uint32_t num_elem_indir1 = FS_BLOCK_SIZE / sizeof(uint32_t); // next 256 blocks
    if (file_inode->size % FS_BLOCK_SIZE != 0){
        num_blks_in_file = num_blks_in_file + 1; // due to some extra bytes.
    }

    // shitty code.
    // free all the blocks in the file.
    // indir_2 block
    uint32_t indir_blks[num_elem_indir1];
    uint32_t indir_blks2[num_elem_indir1];
    uint32_t indir2_ptr_file = file_inode->indir_2;
    if (file_inode->indir_2 != 0){
        // read the block
        if (disk->ops->read(disk, indir2_ptr_file, 1, indir_blks) < 0)
            exit(1);
        uint32_t i;
        for (i=0;i<num_elem_indir1;i++){
            if(i*num_elem_indir1 + N_DIRECT + num_elem_indir1 >= num_blks_in_file){
                break;
            }
            if (indir_blks[i] != 0){
                // fre the things inside that block.
                uint32_t second_indir_addr = indir_blks[i];
                if (disk->ops->read(disk, second_indir_addr, 1, indir_blks2) < 0)
                    exit(1);
                uint32_t j;
                for (j=0;j<num_elem_indir1;j++){
                    if(j + i*num_elem_indir1 + N_DIRECT + num_elem_indir1 >= num_blks_in_file){
                        break;
                    }
                    if (indir_blks2[j] != 0){
                        // fre the things inside that block.
                        set_block_number_free(indir_blks2[j]);
                        indir_blks2[j] = 0; // you don't have to 
                    }else{break;}
                }
                set_block_number_free(indir_blks[i]);
                indir_blks[i] = 0; // you don't have to 
            }else{break;}

        }
        set_block_number_free(file_inode->indir_2);
        file_inode->indir_2 = 0;
    }

    // indir_1 block
    uint32_t indir1_ptr_file = file_inode->indir_1;
    if (file_inode->indir_1 != 0){
        // truncate all the blocks in indir_1
        if (disk->ops->read(disk, indir1_ptr_file, 1, indir_blks) < 0)
            exit(1);
        uint32_t i;
        for (i=0;i<num_elem_indir1;i++){
            if(i + N_DIRECT >= num_blks_in_file){
                break;
            }
            if (indir_blks[i] != 0){
                set_block_number_free(indir_blks[i]);
                indir_blks[i] = 0; // you don't have to write it back to the disk
            }
        }
        set_block_number_free(file_inode->indir_1);
        file_inode->indir_1 = 0;
    }

    // free all direct blocks.
    uint32_t i;
    for (i=0; i< N_DIRECT; i++){
        if (file_inode->direct[i] != 0){
            set_block_number_free(file_inode->direct[i]);
            file_inode->direct[i] = 0;
        }
    }

    // set the file size to zero
    file_inode->size = 0;
    // set file modification time.
    file_inode->mtime = (time_t) time(NULL);
    // write inode list.
    write_inode_to_disk(file_inode_num);
    // write block bitmap.
    write_blockbitmap_to_disk();

    return 0;
}


static int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0)
        return -EINVAL;		/* invalid argument */

    struct path_info pi;
    uint32_t start_blk;
    uint32_t end_blk;

    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }

    // see if it's a directory
    if (pi.is_dir == 1){
        return -EISDIR;
    }

    return truncate_inode(pi.cur_inode);
}

static void remove_inode_from_directory(struct path_info *pi){
    // free the inode
    set_inode_number_free(pi->cur_inode);

    // remove it from dirent of parent.
    struct fs7600_inode *parent_inode = &inode_list[pi->par_inode];
    // read the dirent of parent.
    uint32_t parent_dir_ptr = parent_inode->direct[0];

    struct fs7600_dirent *dir_entry;
    dir_entry = malloc(FS_BLOCK_SIZE);
    // read the parent directory and see if 32 entries issue.
    if(disk->ops->read(disk, parent_dir_ptr, 1, dir_entry) < 0)
       exit(1);
    
    uint32_t num_enteries = FS_BLOCK_SIZE / sizeof(struct fs7600_dirent); // this should always be 32    
    // iterate through the enteries and invalidate that specific entry.       
    uint32_t i;
    for (i=0; i < num_enteries; i++){
        if(dir_entry[i].valid == 1 && dir_entry[i].inode == pi->cur_inode){
            dir_entry[i].valid = 0;
        }
    }
    // write parent dirent.
    if(disk->ops->write(disk, parent_dir_ptr, 1, dir_entry) < 0)
       exit(1);

   free(dir_entry);
}

/* unlink - delete a file
 *  Errors - path resolution, ENOENT, EISDIR
 * Note that you have to delete (i.e. truncate) all the data.
 */
static int fs_unlink(const char *path)
{
    struct path_info pi;
    uint32_t start_blk;
    uint32_t end_blk;

    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }

    // see if it's a directory
    if (pi.is_dir == 1){
        return -EISDIR;
    }
    // truncate the file.
    truncate_inode(pi.cur_inode);
    remove_inode_from_directory(&pi);
    // remove from cache
    if (homework_part > 2){
        flush_dir_entry_cache_entry(pi.cur_inode);
    }

    return 0;
}

/* rmdir - remove a directory
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
static int fs_rmdir(const char *path)
{
    struct path_info pi;
    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }

    // see if it's a directory
    if (pi.is_dir != 1){
        return -ENOTDIR;
    }
    struct fs7600_inode *file_inode = &inode_list[pi.cur_inode];

    // see if it's empty or not.
    struct fs7600_dirent *dir_entry;
    dir_entry = malloc(FS_BLOCK_SIZE);
    // read the parent directory and see if 32 entries issue.
    uint32_t dir_ptr = file_inode->direct[0];
    if(disk->ops->read(disk, dir_ptr, 1, dir_entry) < 0)
       exit(1);
    
    uint32_t num_enteries = FS_BLOCK_SIZE / sizeof(struct fs7600_dirent); // this should always be 32    
    // iterate through the enteries and invalidate that specific entry.       
    uint32_t i;
    for (i=0; i < num_enteries; i++){
        if(dir_entry[i].valid == 1){
            free(dir_entry);
            return -ENOTEMPTY;
        }
    }
    free(dir_entry);

    // free the direct 0 block.
    if (file_inode->direct[0] != 0){
        set_block_number_free(file_inode->direct[0]);
        file_inode->indir_1 = 0;
    }

    remove_inode_from_directory(&pi);
    // remove cache entry
    if (homework_part > 2){
        flush_dir_entry_cache_entry(pi.cur_inode);
    }

    return 0;

}

/* rename - rename a file or directory
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
static int fs_rename(const char *src_path, const char *dst_path)
{
    struct path_info src_pi;
    struct path_info dest_pi;
    struct path_info dest_parent_pi;

    // ENOENT - source does not exist
    if (get_path(src_path, &src_pi) < 0) {
        return -ENOENT;
    }

    // * EEXIST - destination already exists
    if (get_path(dst_path, &dest_pi) >= 0) {
        return -EEXIST;
    }

    // * EINVAL - source and destination are not in the same directory
    // get parent of destination.
    char* parent_dir_path = get_parent_directory(dst_path);
    char *path_copy = strdup(parent_dir_path);
    free(parent_dir_path);
    if (get_path(path_copy, &dest_parent_pi) < 0) {
        free(path_copy);
        return -EINVAL;
    }
    free(path_copy);

    // if both don't have the same parent.
    if(src_pi.par_inode != dest_parent_pi.cur_inode){
        return -EINVAL;
    }

    // get parent dirent.
    struct fs7600_inode *parent_inode = &inode_list[src_pi.par_inode];
    struct fs7600_dirent *dir_entry;
    dir_entry = malloc(FS_BLOCK_SIZE);

    // read the parent directory and see if 32 entries issue.
    uint32_t dir_ptr = parent_inode->direct[0];
    if(disk->ops->read(disk, dir_ptr, 1, dir_entry) < 0)
       exit(1);
    uint32_t num_enteries = FS_BLOCK_SIZE / sizeof(struct fs7600_dirent); // this should always be 32    

    // rename the block
    uint32_t i;
    for (i=0; i < num_enteries; i++){
        if(dir_entry[i].valid == 1 && dir_entry[i].inode == src_pi.cur_inode){
            strcpy(dir_entry[i].name, dest_pi.file_name);
        }
    }
    // write parent dirent.
    if(disk->ops->write(disk, dir_ptr, 1, dir_entry) < 0)
       exit(1);

    free(dir_entry);
    // write to cache.
    if (homework_part > 2){
        rename_dir_entry_cache_entry(src_pi.cur_inode, dest_pi.file_name);
    }

    return 0;
}



/* chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * Errors - path resolution, ENOENT.
 */

static int chmod_inode(int inode, mode_t mode){

    inode_list[inode].mode = mode;
    write_inode_to_disk(inode);
    return 0;
}

static int utime_inode(int inode, struct utimbuf *ut){

    inode_list[inode].mtime = ut->modtime;
    write_inode_to_disk(inode);
    return 0;
}

static int fs_chmod(const char *path, mode_t mode)
{
    struct path_info pi;
    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }

    return chmod_inode(pi.cur_inode, mode);    
}

int fs_utime(const char *path, struct utimbuf *ut)
{
    struct path_info pi;
    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }

    return utime_inode(pi.cur_inode, ut);
}

static int set_block_location(struct fs7600_inode *file_inode, uint32_t blk_number, uint32_t blk_addr, uint32_t *indir_blks_1, uint32_t *indir_blks_2, struct file_temp_info *file_temp_info_write_file){
    if (blk_number < N_DIRECT){
        file_inode->direct[blk_number] = blk_addr;
        return 0;
    }
    // indir_1 is an array of pointers
    uint32_t num_elem_indir1 = FS_BLOCK_SIZE / sizeof(uint32_t); // next 256 blocks

    uint32_t indir_blks[num_elem_indir1];
    // if file_inode->indir1 is zero
    memset(indir_blks, 0, sizeof(indir_blks)); // set block to zero.
    uint32_t indir1_ptr_file = file_inode->indir_1; 
    if (file_inode->indir_1 == 0){
        // allocate a new block.
        int free_block = get_free_block_number();
        if (free_block < 0){
            return -1;
        }
        // initialise block to zero.
        file_inode->indir_1 = free_block;
        indir1_ptr_file = file_inode->indir_1;
        //memset(indir_blks_1, 0, FS_BLOCK_SIZE); // set block to zero.
        //if (disk->ops->write(disk, indir_1_ptr_file, 1, indir_blks) < 0)
        //    exit(1);
        file_temp_info_write_file->indir_1_read_from_disk = 1;
    }
    if(file_temp_info_write_file->indir_1_read_from_disk == 0){
        if (disk->ops->read(disk, indir1_ptr_file, 1, indir_blks_1) < 0)
            exit(1);
        file_temp_info_write_file->indir_1_read_from_disk = 1;
    }
    if (blk_number < N_DIRECT + num_elem_indir1){
        indir_blks_1[blk_number - N_DIRECT] = blk_addr; // subtract since we have 6-271 here(indexed at 0-255).
        //file_temp_info_write_file->indir_1_modified = 1;
        return 0;
    }
    // if indir2 is zero.
    uint32_t indir2_ptr_file = file_inode->indir_2; 
    if (file_inode->indir_2 == 0){
        // allocate a new block.
        int free_block = get_free_block_number();
        if (free_block < 0){
            return -1;
        }
        file_inode->indir_2 = free_block;
        indir2_ptr_file = file_inode->indir_2;
        // write all zeros in this block.
        //memset(indir_blks_2, 0, FS_BLOCK_SIZE); // set block to zero.
        //if (disk->ops->write(disk, indir2_ptr_file, 1, indir_blks) < 0)
        //    exit(1);
        file_temp_info_write_file->indir_2_read_from_disk = 1;
    }
    // read the indir ptr if not already there.
    if(file_temp_info_write_file->indir_2_read_from_disk == 0){
        if (disk->ops->read(disk, indir2_ptr_file, 1, indir_blks_2) < 0)
            exit(1);
        file_temp_info_write_file->indir_2_read_from_disk = 1;
    }
    // indir_2 is an array of pointers to pointers
    uint32_t blk_num_indir2 = blk_number - (num_elem_indir1 + N_DIRECT);
    // find the the pointer to the indir1
    uint32_t blk_indir2_ptr = blk_num_indir2 / num_elem_indir1;  // div by 256.
    // now read the address of this pointer from disk
    //file_temp_info_write_file->indir_2_modified = 2;
    uint32_t file_addr_blk = indir_blks_2[blk_indir2_ptr];

    // if this file_addr_blk is zero. i.e second indir blk doesn't exist
    if (file_addr_blk == 0){
        int free_block = get_free_block_number();
        if (free_block < 0){
            return -1;
        }
        // set this block in the indir2 pointer and write 
        indir_blks_2[blk_indir2_ptr] = free_block;
        file_addr_blk = free_block;
        // now write this.
        //if (disk->ops->write(disk, indir2_ptr_file, 1, indir_blks) < 0)
        //    exit(1);

        // memset it to zero
        memset(indir_blks, 0, sizeof(indir_blks)); // set block to zero.
        // fill that block
        uint32_t blk_indir2 = blk_num_indir2 % num_elem_indir1;
        indir_blks[blk_indir2] = blk_addr;
        if (disk->ops->write(disk, file_addr_blk, 1, indir_blks) < 0)
            exit(1);
        return 0;
    }

    if (disk->ops->read(disk, file_addr_blk, 1, indir_blks) < 0)
        exit(1);
    // return the corresponding block
    uint32_t blk_indir2 = blk_num_indir2 % num_elem_indir1;
    indir_blks[blk_indir2] = blk_addr;
    if (disk->ops->write(disk, file_addr_blk, 1, indir_blks) < 0)
        exit(1);
    return 0;
}

static uint32_t get_block_location(struct fs7600_inode *file_inode, uint32_t blk_number, uint32_t *indir_blks_1, uint32_t *indir_blks_2, struct file_temp_info *file_temp_info_read_file){
    // assumes block exists already.
    if (blk_number < N_DIRECT){
        return file_inode->direct[blk_number];
    }
    uint32_t num_elem_indir1 = FS_BLOCK_SIZE / sizeof(uint32_t); // next 256 blocks
    uint32_t indir_blks[num_elem_indir1];
    // indir_1 is an array of pointers
    uint32_t indir1_ptr_file = file_inode->indir_1;
    uint32_t indir2_ptr_file = file_inode->indir_2;
    if (blk_number < N_DIRECT + num_elem_indir1){
        if (file_temp_info_read_file->indir_1_read_from_disk == 0){
            if (disk->ops->read(disk, indir1_ptr_file, 1, indir_blks_1) < 0)
                exit(1);            
            file_temp_info_read_file->indir_1_read_from_disk = 1;
        }
        return indir_blks_1[blk_number - N_DIRECT]; // subtract since we have 6-271 here(indexed at 0-255).
    }
    // indir_2 is an array of pointers to pointers
    uint32_t blk_num_indir2 = blk_number - (num_elem_indir1 + N_DIRECT);
    if (file_temp_info_read_file->indir_2_read_from_disk == 0){
        if (disk->ops->read(disk, indir2_ptr_file, 1, indir_blks_2) < 0)
            exit(1);
        file_temp_info_read_file->indir_2_read_from_disk = 1;
    }

    // find the the pointer to the indir1
    uint32_t blk_indir2_ptr = blk_num_indir2 / num_elem_indir1;  // div by 256.
    // now read the address of this pointer from disk
    uint32_t file_addr_blk = indir_blks_2[blk_indir2_ptr];
    if (disk->ops->read(disk, file_addr_blk, 1, indir_blks) < 0)
        exit(1);
    // return the corresponding block
    uint32_t blk_indir2 = blk_num_indir2 % num_elem_indir1;
    return indir_blks[blk_indir2];
}

/* read - read data from an open file.
 * should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return bytes from offset to EOF
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
static int fs_read(const char *path, char *buf, size_t len, off_t offset,
		    struct fuse_file_info *fi)
{
    struct path_info pi;
    uint32_t start_blk;
    uint32_t end_blk;
    struct file_temp_info file_temp_info_read_file;
    memset(&file_temp_info_read_file, 0, sizeof(struct file_temp_info));
    uint32_t num_elem_indir1 = FS_BLOCK_SIZE / sizeof(uint32_t); // next 256 blocks
    uint32_t indir_blks_1[num_elem_indir1];
    uint32_t indir_blks_2[num_elem_indir1];


    // get path
    if (homework_part == 1){
        if (get_path(path, &pi) < 0) {
            return -ENOENT;
        }

        // see if it's a directory
        if (pi.is_dir == 1){
            return -EISDIR;
        }
    }
    else{
        pi.cur_inode = fi->fh;        
    }

    struct fs7600_inode *file_inode = &inode_list[pi.cur_inode];

    // if offset >= file len, return 0
    if (offset >= file_inode->size){
        return 0;    
    }
    
    size_t read_len;
    //if offset+len > file len, return bytes from offset to EOF
    if (offset + len > file_inode->size){
        read_len = file_inode->size - offset;
    }
    else{
        read_len = len;
    }

    start_blk = offset / FS_BLOCK_SIZE;
    end_blk = (offset + read_len) / FS_BLOCK_SIZE;
    if ((offset + read_len) % FS_BLOCK_SIZE == 0){
        end_blk -=1;
    }
    uint32_t num_blks = end_blk - start_blk + 1; // +1 since we're including everything.
    char *temp_buffer;
    temp_buffer = malloc(num_blks*FS_BLOCK_SIZE);
    // read the file size
    uint32_t iter_blk;
    uint32_t blk_num;
    for (iter_blk= 0; iter_blk < num_blks; iter_blk++){
        // get block location
        blk_num = iter_blk + start_blk;
        uint32_t blk_addr = get_block_location(file_inode, blk_num, indir_blks_1, indir_blks_2, &file_temp_info_read_file);
        // read one block at a time, since these blocks may not be colocated on the disk.
        char *buffer_ptr = &temp_buffer[iter_blk * FS_BLOCK_SIZE];
        if(disk->ops->read(disk, blk_addr, 1, buffer_ptr))
            exit(1);
    }
    uint32_t read_position = offset % FS_BLOCK_SIZE;
    char *temp_buffer_position = &temp_buffer[read_position];
    // read 
    memcpy(buf, temp_buffer_position, len);
    free(temp_buffer);
    return read_len;
}

/* write - write data to a file
 * It should return exactly the number of bytes requested, except on
 * error.
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them, 
 *   but we don't)
 */
static int fs_write(const char *path, const char *buf, size_t len,
		     off_t offset, struct fuse_file_info *fi)
{
    struct path_info pi;
    uint32_t start_blk;
    uint32_t end_blk;
    struct file_temp_info file_temp_info_write_file;
    memset(&file_temp_info_write_file, 0, sizeof(struct file_temp_info));

    if (homework_part == 1){
        if (get_path(path, &pi) < 0) {
            return -ENOENT;
        }

        // see if it's a directory
        if (pi.is_dir == 1){
            return -EISDIR;
        }
    }
    else{
        pi.cur_inode = fi->fh;        
    }

    struct fs7600_inode *file_inode = &inode_list[pi.cur_inode];

    // if offset >= file len, return EINVAL
    if (offset > file_inode->size){
        return -EINVAL;    
    }

    // let the write begin.
    // 3 cases. start blk, all regular blcks, end blk.
    // get the start block.
    start_blk = offset / FS_BLOCK_SIZE;
    end_blk = (offset + len) / FS_BLOCK_SIZE;
    if ((offset + len) % FS_BLOCK_SIZE == 0){
        end_blk -=1;
    }
    uint32_t num_blks = end_blk - start_blk + 1; // +1 since we're including everything.
    uint32_t num_blks_in_file = file_inode->size / FS_BLOCK_SIZE; // due to some extra bytes.
    if (file_inode->size % FS_BLOCK_SIZE != 0){
        num_blks_in_file = num_blks_in_file + 1; // due to some extra bytes.
    }
    char *temp_buffer;
    temp_buffer = malloc(num_blks*FS_BLOCK_SIZE);
    memset(temp_buffer,0, num_blks*FS_BLOCK_SIZE);

    //-----------------------------------------------------
    //start blk data | data to write | end blk data(if any)|
    //-----------------------------------------------------
    uint32_t num_elem_indir1 = FS_BLOCK_SIZE / sizeof(uint32_t); // next 256 blocks
    uint32_t indir_blks_1[num_elem_indir1];
    uint32_t indir_blks_2[num_elem_indir1];
    memset(indir_blks_1, 0,sizeof(indir_blks_1)); // set block to zero.
    memset(indir_blks_2, 0,sizeof(indir_blks_2)); // set block to zero.

    // read the start block
    if (start_blk < num_blks_in_file){
        uint32_t start_blkaddr = get_block_location(file_inode, start_blk, indir_blks_1, indir_blks_2, &file_temp_info_write_file);
        if (disk->ops->read(disk, start_blkaddr, 1, temp_buffer) < 0)
            exit(1);
    }
    // read the end block if it exists.
    if (end_blk < num_blks_in_file){
        uint32_t end_blkaddr = get_block_location(file_inode, end_blk, indir_blks_1, indir_blks_2, &file_temp_info_write_file);
        if (disk->ops->read(disk, end_blkaddr, 1, &temp_buffer[(num_blks-1)*FS_BLOCK_SIZE]) < 0)
            exit(1);
    }
    uint32_t strt_blk_data_offset = offset % FS_BLOCK_SIZE;
    uint32_t end_blk_data_offset = (FS_BLOCK_SIZE - ((offset+len) % FS_BLOCK_SIZE)) % FS_BLOCK_SIZE;    
    // write your data to temp buffer.
    memcpy(&temp_buffer[strt_blk_data_offset], buf, len); 

    uint32_t disk_space_exceed = 0;
    // num bytes written to disk
    int num_bytes_written = 0 - strt_blk_data_offset;

    uint32_t iter_blk;
    uint32_t blk_num;
    int free_block = -1;
    uint32_t blk_addr;

    for (iter_blk= 0; iter_blk < num_blks; iter_blk++){
        // get block location
        blk_num = iter_blk + start_blk;
        if (blk_num < num_blks_in_file){
            blk_addr = get_block_location(file_inode, blk_num, indir_blks_1, indir_blks_2, &file_temp_info_write_file);
            // read one block at a time, since these blocks may not be colocated on the disk.
            if (blk_addr <= 0 || blk_addr > sb_memory.num_blocks){
                disk_space_exceed = 1;      
                break;
            }
        }else{
            free_block = get_free_block_number();
            if (free_block < 0){
                disk_space_exceed = 1;      
                break;
            }
            blk_addr = (uint32_t) free_block;
            // now allocate this block.
            if(set_block_location(file_inode, blk_num, blk_addr, indir_blks_1, indir_blks_2, &file_temp_info_write_file) < 0){
                disk_space_exceed = 1;
                break;
            }
        }
        char *buffer_ptr = &temp_buffer[iter_blk * FS_BLOCK_SIZE];
        if(disk->ops->write(disk, blk_addr, 1, buffer_ptr))
            exit(1);
        num_bytes_written += FS_BLOCK_SIZE;
    }
    // remove end block stuff from num bytes written.
    if (disk_space_exceed == 0){
        num_bytes_written -= end_blk_data_offset;
    }

    if (file_inode->size < offset + num_bytes_written){
        // modify the inode
        file_inode->size = offset + num_bytes_written;
    }
    // write back indir_blk 1 and 2 if they've been modified.
    uint32_t indir2_ptr_file = file_inode->indir_2;
    if (indir2_ptr_file != 0){
        if (disk->ops->write(disk, indir2_ptr_file, 1, indir_blks_2) < 0)
            exit(1);        
    }
    uint32_t indir1_ptr_file = file_inode->indir_1;
    if (indir1_ptr_file != 0){
        if (disk->ops->write(disk, indir1_ptr_file, 1, indir_blks_1) < 0)
            exit(1);
    }
    // modify mtime and write to disk write to disk
    file_inode->mtime = (time_t) time(NULL);
    write_inode_to_disk(pi.cur_inode);
    // write block bitmap.
    write_blockbitmap_to_disk();

    free(temp_buffer);
    return num_bytes_written;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
    if (homework_part == 1){
        return 0;    
    }

    struct path_info pi;
    if (get_path(path, &pi) < 0) {
        return -ENOENT;
    }

    if (pi.is_dir == 1){
        return -EISDIR;
    }

    fi->fh = pi.cur_inode;

    return 0;
}

static int fs_release(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. 
 */
static int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (set others to zero):
     *   f_bsize = BLOCK_SIZE
     *   f_blocks = total image - metadata
     *   f_bfree = f_blocks - blocks used
     *   f_bavail = f_bfree
     *   f_namelen = <whatever your max namelength is>
     *
     * this should work fine, but you may want to add code to
     * calculate the correct values later.
     */
    st->f_bsize = FS_BLOCK_SIZE;
    st->f_blocks = 0;           /* probably want to */
    st->f_bfree = 0;            /* change these */
    st->f_bavail = 0;           /* values */
    st->f_namemax = 27;

    return 0;
}

void *fs_destroy(void *ignore)
{
    if (homework_part > 3){
        int i;
        for (i=0; i<DIRTY_CACHE_SIZE;i++){
            write_back_cache_flush_dirty_entry(disk, i);
        }
    }
    free(inode_list);
    free(inode_map);
    free(block_map);
}

/* operations vector. Please don't rename it, as the skeleton code in
 * misc.c assumes it is named 'fs_ops'.
 */
struct fuse_operations fs_ops = {
    .init = fs_init,
    .getattr = fs_getattr,
    .opendir = fs_opendir,
    .readdir = fs_readdir,
    .releasedir = fs_releasedir,
    .mknod = fs_mknod,
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .rename = fs_rename,
    .chmod = fs_chmod,
    .utime = fs_utime,
    .truncate = fs_truncate,
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .release = fs_release,
    .statfs = fs_statfs,
    .destroy = fs_destroy,
};

