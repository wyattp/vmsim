/*
 * CMPUT xxx - Assignment x
 * Virtual Memory Simulator
 *
 * Auth:    Wyatt Praharenka
 * CCID:    
 * SID:     
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <time.h>
#include <fcntl.h>

/* 
 * reference string operations
 */
#define OP_INC  0
#define OP_DEC  1
#define OP_WR   2
#define OP_RD   3

/*
 * bitfield definitions in # of bits
 */
#define VAL_BITS    6
#define OP_BITS     2
#define PG_BITS     24

/*
 * size of hash table for none
 * replacement. these numbers are
 * absolute and should handle most
 * inputs.
 */
#define HASH_SIZE	1000
#define HASH_LEN	400

#define PROG_NAME "a4vmsim"

/*
 * reference string bitfield
 */
struct ref {
    unsigned int    val:    VAL_BITS;
    unsigned int    op :    OP_BITS;
    unsigned int    pg :    PG_BITS;
};

/*
 * page frame
 */
struct frame {
    int             used;
    int             dirty;
    int             ref;
    unsigned int    pg;
    int             last_use;
};

/*
 * stats structure
 */
struct vm_stats {
    int     writes;
    int     faults;
    int     accum;
    int     flushes;
    clock_t start;
    clock_t end;
};

/*
 * reference history for last 3 accesses
 */
struct hist {
    unsigned int     p1;         /* 'page1' */
    unsigned int     p2;
    unsigned int     p3;
};

struct hash_entry {
	unsigned int	page;
	unsigned int	frame_num;
    unsigned int    used;       /* all will be 0 at first */
    unsigned int    term;
};

struct hash_arr {
    int     n;
    int     len;
    struct hash_entry *arr;
};

/*
 * functions
 */
void    read_page (unsigned int page);
void    write_page (unsigned int page);
double  pr_time (clock_t real);
void    insert_page (int pos, unsigned int page, int last_use);
void    hist_push (unsigned int page, struct hist *h);
int		logt (int num);

/* replacement straegies */
struct frame * search_mem (unsigned int page); 
struct frame * none_replacement (unsigned int page);
struct frame * mrand_replacement (unsigned int page);
struct frame * lru_replacement (unsigned int page);
struct frame * sec_replacement (unsigned int page);

/*
 * global vars
 */
int pagesize;
int memsize;
int npages;
int rstrat;
int ref_num;

struct frame *  page_frames;
struct ref      reference;
//struct hash_entry	page_hash[HASH_SIZE][HASH_LEN];
struct hash_arr     page_hash[HASH_SIZE];


struct vm_stats stats = {0};
struct hist     ref_hist;

struct frame * (*replace)(unsigned int);

void
usage (void) {
    printf ("Usage:\t./a4vsim pagesize memsize strategy\n"
            "\n"
            "\tPagesize is between 256 and 8192\n"
            "\tStrategy is one of 'none', 'mrand', 'lru', 'sec'\n");
}

void
dprintff (struct ref *refr) {
    printf ("REF NUM:   %d\n"
            "PG:        %d\n"
            "OP:        %d\n"
            "VAL:       %d\n\n",
            ref_num, refr->pg, refr->op, refr->val);
}

int
main (int argc, char *argv[]) {
    int err, page_shift, i;
    char *strat;


    /*
     * initalize hash table
     */
    for (i = 0; i < HASH_SIZE; i++) {
        page_hash[i].arr = malloc (sizeof(struct hash_entry) * HASH_LEN);
        page_hash[i].len = HASH_LEN;
        page_hash[i].n = 0;
    }

    /* init random */
    srand (time (NULL));

    /*
     * params
     */
    if (argc < 4) {
        usage();
        return -1;
    }

    pagesize = atoi (argv[1]);
    memsize = atoi (argv[2]);
    strat = argv[3];

    if (pagesize > 8192 || pagesize < 256) {
        printf ("Value of pagesize needs to be between 256 and 8192 bytes\n");
        return -1;
    }

	/* build page mask */
	if ((logt (pagesize)) < 0) {
		printf ("Please ensure the pagesize is a power of two\n");
		return -1;
	}
    page_shift = 24 - (32 - logt (pagesize));

    npages = (memsize/pagesize + (memsize % pagesize != 0));
    memsize = npages * pagesize;

    /* set replacement strategy */
    if ((err = !strcmp ("none", strat)))
        replace = &none_replacement;
    if (!err && (err = !strcmp ("mrand", strat)))
        replace = &mrand_replacement;
    if (!err && (err = !strcmp ("lru", strat)))
        replace = &lru_replacement;
    if (!err && (err = !strcmp ("sec", strat)))
        replace = &sec_replacement;

    if (!err) {
        printf ("Page replacement strategy needs to be one of: none, mrand, lru, sec\n");
        return -1;
    }

    /*
     * start sim
     */
    printf ("a4vsim [page= %d, mem= %d, %s, page num= %d]\n"
            , pagesize, memsize, strat, npages);

    page_frames = malloc ((npages) * sizeof (struct frame));
    ref_num = 0;

    stats.start = clock();
    while (read (STDIN_FILENO, &reference, 4) > 0) {

		reference.pg = reference.pg >> page_shift;

        switch (reference.op) {

        case OP_INC:
            read_page (reference.pg);
            stats.accum += reference.val;
            break;

        case OP_DEC:
            read_page (reference.pg);
            stats.accum -= reference.val;
            break;
            
        case OP_WR:
            write_page (reference.pg);
            stats.writes += 1;
            break;

        case OP_RD:
            read_page (reference.pg);
            break;

        default:
            fprintf (stderr, "[%s] Error: Illegal opcode, received %d\n"
                    ,PROG_NAME, reference.op);

        }
        ref_num++;

    }
    stats.end = clock();

    printf ("[%s] %d references processed using '%s' in %.2f sec.\n"
            , PROG_NAME, ref_num, strat, pr_time(stats.end-stats.start));
    printf ("[%s] page faults= %d, write count= %d, flushes= %d\n"
            , PROG_NAME, stats.faults, stats.writes, stats.flushes);
    printf ("[%s] Accumulator= %d\n", PROG_NAME, stats.accum);

    /*
     * clean up
     */
    free (page_frames);
    
    for (i = 0; i < HASH_SIZE; i++)
        free(page_hash[i].arr);

    return 0;
}

/*
 * attempt to read page, if page is not in memory
 * throw a page fault and bring into memory and execute
 * the replacement algorithm
 */
void
read_page (unsigned int page) {
    struct frame *fr;

    hist_push (page, &ref_hist);
    fr = search_mem(page);

    if (fr == NULL) {
        stats.faults++;
        fr = replace (page);
    }
    else
        fr->ref = 1;

    fr->last_use = ref_num;
}

/*
 * attempt to write page, if page is not in memory
 * throw a page fault and bring into memory and execute
 * the replacement algorithm then write to the page
 */
void
write_page (unsigned int page) {
    struct frame *fr;
    
    hist_push (page, &ref_hist);
    fr = search_mem(page);

    if (fr == NULL) {
        stats.faults++;
        fr = replace (page);
    }
    else
        fr->ref = 1;

    fr->last_use = ref_num;
    fr->dirty = 1;
}

/*
 * make a reference to a page. bring this frame into
 * memory if the page is not already memory resident
 *
 * returns NULL if no page is found, otherwise return
 * a pointer to the current frame in memory
 */
struct frame *
search_mem (unsigned int page) {
    int i, hashn;
    struct hash_entry   *hash_ent;

    hashn = page_hash[page % HASH_SIZE].n;
	for (i = 0; i < hashn; i++) {
        hash_ent = &page_hash[page % HASH_SIZE].arr[i];

        if (!hash_ent->used)
            return NULL;

		if (hash_ent->page == page) {

			if (page_frames[hash_ent->frame_num].pg == page)
				return &page_frames[hash_ent->frame_num];
			else
				return NULL;

		}
	}

	return NULL;
}

/*
 * NOTE: replace () is a pointer to one of the following
 */

struct frame *
none_replacement (unsigned int page) {
    static  int i = 0;

    if (i >= npages) {
        npages += 1000;
        page_frames = realloc (page_frames, (npages)*sizeof (struct frame));
    }

    insert_page (i, page, ref_num);

    return &page_frames[i++];
}

struct frame *
mrand_replacement (unsigned int page) {
    int             pos;

    while ((pos = rand()%npages) == ref_hist.p1
            || pos == ref_hist.p2
            || pos == ref_hist.p3);

	if (page_frames[pos].dirty)
		stats.flushes++;

    insert_page (pos, page, ref_num); 
    
    return &page_frames[pos];
}

/*
 * least recently used page replacement
 */
struct frame *
lru_replacement (unsigned int page) {
    int i, ref_min = -1, ref_min_index;

    for (i = 0; i < npages && page_frames[i].used > 0; i++) {
        if (ref_min < 0 || page_frames[i].last_use < ref_min) {
            ref_min = page_frames[i].last_use;
            ref_min_index = i;
        }
    }

    if (i == npages)        /* no empty frame found */
        i = ref_min_index;

    if (page_frames[i].dirty)
        stats.flushes++;

    /* add page */
    insert_page (i, page, ref_num);

    return &page_frames[i];
}

/*
 * second chance replacement
 */
struct frame *
sec_replacement (unsigned int page) {
    static int  pos = 0;
    static int  full = 0;
    int         ret_pos;

    /* page table full, go through reference bits */
    while (full && page_frames[pos].ref == 1) {
        page_frames[pos].ref = 0;
        if (++pos >= npages)
            pos = 0;
    }

    if (page_frames[pos].dirty)
        stats.flushes++;

    /* add page */
    insert_page (pos, page, ref_num);
    ret_pos = pos;

    if (++pos >= npages) {
        pos = 0;
        full = 1;
    }

    return &page_frames[ret_pos];
}

void
insert_page (int pos, unsigned int page, int last_use) {
	int				i, hashn, hashlen;
	unsigned int	index;
    struct hash_entry *hash_ent;

	index = page % HASH_SIZE;
    hash_ent = page_hash[index].arr;
    hashlen = page_hash[index].len;
    hashn = page_hash[index].n;

	/* insert to hash table */
    for (i = 0; i < hashn && i < hashlen && hash_ent[i].page != page; i++);

    /* out of space */
    if (i == hashlen) {
        printf ("out of space\n");
        page_hash[index].arr = realloc (page_hash[index].arr, 2*hashlen*sizeof (struct hash_entry));
        page_hash[index].n = 2*hashlen;
    }

	page_hash[index].arr[i].page = page;
	page_hash[index].arr[i].frame_num = pos;
    page_hash[index].arr[i].used = 1;

    if (i == hashn)
        page_hash[index].n++; 

    page_frames[pos].pg = page;
    page_frames[pos].used = 1;
    page_frames[pos].dirty = 0;
    page_frames[pos].last_use = last_use;
    page_frames[pos].ref = 0;

}

/*
 * push page to history table
 */
void
hist_push (unsigned int page, struct hist *h) {
    h->p3   = h->p2;
    h->p2   = h->p1;
    h->p1   = page;
}

double
pr_time (clock_t real) {
    return real / (double) CLOCKS_PER_SEC;
}

/*
 * Must be a power of two
 */
int
logt (int num) {
	int comp, i;

	for (i=0,comp=1; i < 32; i++) {
		if (comp == num)
			return i;
		comp = comp << 1;
	}

	return -1;
}
