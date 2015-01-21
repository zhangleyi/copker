#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/tty.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#define info(fmt, args...) do { printk("[core-%d] "fmt"\n", cpu,## args); } while (0)
#define debug(fmt, args...) do { printk("[debug] "fmt"\n", ## args); } while (0)

#define CORES 2 //we only use core0 and core1
#define sleep_millisecs 1000

#define Bit_29 0x20000000
#define Bit_30 0x40000000

#define CACHE_STACK_SIZE 256
#define KEY_LENGTH_IN_BYTES 128 //1024bit
#define CACHE_LINE_SIZE 64

static unsigned long start[CORES];
static unsigned long dur[CORES];

static 	struct task_struct *t_thread[CORES];

static int cache_frozen = 0;
static int env_clear = 0;

typedef struct cacheCryptoEnv{
	unsigned char masterKey[128/8];
	unsigned char privateKey[KEY_LENGTH_IN_BYTES];
	unsigned char cacheStack[CACHE_STACK_SIZE];
	unsigned char in[KEY_LENGTH_IN_BYTES];
	unsigned char out[KEY_LENGTH_IN_BYTES];
	unsigned int privateKeyId;	
	//unsigned char out[(KEY_LENGTH_IN_BYTE + CACHE_LINE_SIZE - 1)/ CACHE_LINE_SIZE	
	//	*CACHE_LINE_SIZE]__attribute__ ((aligned(CACHE_LINE_SIZE)));	
}cacheCryptoEnv;

asmlinkage void switch_stack(void *para, void *function,unsigned char *stackBottom);

int isCacheWriteBack(void)
{
	int result = 0;
	int value_of_cr0 = 0;
	
	asm volatile("movl %%cr0,%%eax\n\t"
		"movl %%eax,%0\n\t"
		: "=m" (value_of_cr0)
		:
		:"eax");
		
	//info("CR0: %x\n",value_of_cr0);	
	
	if((value_of_cr0 & Bit_29) == 0 && (value_of_cr0 & Bit_30) == 0)
		result = 1;
		
	return result;		
}

//try wbind first,try invd then
void exit_no_fill(void)
{

    asm volatile("push   %eax\n\t"
                "mov    %cr0,%eax;\n\t"
                "and     $~(1 << 30),%eax;\n\t"
                "mov    %eax,%cr0;\n\t"
                "wbinvd\n\t"
                "pop    %eax");
}

void modify_cr0(unsigned int *cr0)
{
	unsigned int old_cr0 = 0,new_cr0 = 0;
    asm volatile("push   %%eax\n\t"
                "mov    %%cr0,%%eax;\n\t"
                "mov    %%eax,%0;\n\t"
                "or     $(1 << 30),%%eax;\n\t"
                "mov    %%eax,%%cr0;\n\t"
                "mov    %%eax,%1;\n\t"
                "wbinvd\n\t"
                "pop    %%eax"
                :"=m" (old_cr0),"=m" (new_cr0));
    *cr0 = new_cr0;   
    //printk("old_cr0=%x *cr0=%x\n",old_cr0,*cr0);            
}

void enter_no_fill(void)
{

	unsigned int new_value_of_cr0 = 0;
	unsigned long irq_flags;
	unsigned int cpu = 0;

	cpu = smp_processor_id();
	
	preempt_disable();
	local_irq_save(irq_flags); 

	info("Trying to switch cache to no-fill mode");

	/*
		how to use #define in asm?
	*/

	//modify cr0 and print cr0
	modify_cr0(&new_value_of_cr0);
	info("CR0: %x",new_value_of_cr0);

	local_irq_restore(irq_flags);
	preempt_enable();
}

/*
	The instruction of "invd" can't be excuted.
*/
void clear_env(cacheCryptoEnv *env)
{
	unsigned int cpu = smp_processor_id();
	//flush the result in out,
	clflush_cache_range(env->out, KEY_LENGTH_IN_BYTES);

	//then invalidate all the entries in cache
    asm volatile("invd\n\t");	
    info("env has been cleared!");
}

void decrypt_prikey(char *masterkey,char *privatekey)
{
	int len1 = 0,len2 = 0;
	int i = 0;

	if(!masterkey || !privatekey) {
		printk("Key is NULL\n");
		return;
	}

	len1 = strlen(masterkey);
	len2 = strlen(privatekey);

	for(i = 0;i < (len2 - 1);i++) {
		privatekey[i] = privatekey[i] ^ masterkey[i%len1];
	}
}

/*
private_key_compute:
	env->out[i] = (char)(env->in[i] ^ env->privateKey[i])
*/
asmlinkage	void private_key_compute(cacheCryptoEnv *env)
{
	int i = 0;
	int len_msg = 0,len_key = 0;

	if(!env) {
		printk("Error! Env hasn't been initialized.\n");
		return;
	}
		
	decrypt_prikey(env->masterKey,env->privateKey);
	//info("PrivateKey in plaintext:");
	//info("%s",env->privateKey);
	
	len_msg = strlen(env->in);
	len_key = strlen(env->privateKey);

	if(len_msg > KEY_LENGTH_IN_BYTES) {
		printk("Error! Message is too long.\n");
		return;		
	}

	for(i = 0;i < (len_msg - 1);i++) {
		env->out[i] = (char)(env->in[i] - env->privateKey[i] + i );
		//env->out[i] = 'b';
	}
	env->out[i] = '\0';
	return;
	
}

void load_masterkey_from_debugreg(char *key,int len)
{
	int i = 0;

	if(!key) {
		printk("Key is NULL.\n");
		return;
	}

	//*key = A;//not sure if ""*key = "A"  works
	for(i = 0;i < (len - 1);i++)
		key[i] = (char)('A' + i%26);
	key[len - 1] = '\0';		
}

void load_privatekey(int keyid,char *key, int len)
{
	int i = 0;
	int test = 0;

	if(!key) {
		printk("Key is NULL\n");
		return;
	}

	test = keyid;
	//*key = A;//not sure if ""*key = "A"  works
	for(i = 0;i < (len - 1);i++)
		key[i] = (char)('a' + i%26);	
	key[len - 1] = '\0';
}

/*
	we put cacheCryptoEnvto the
L1D cache of the core by reading and writing back one byte
of each cache line incacheCryptoEnv.
*/
void fill_L1D(cacheCryptoEnv *env)
{
	int i = 0;
	char tmp;
	unsigned int cpu = smp_processor_id();

	//load masterkey
	load_masterkey_from_debugreg(env->masterKey,128/8);
	//info("masterkey:");
	//info("%s",env->masterKey);
	
	//read and write cachestack each time one cacheline,seems don't work
	for(i = 0;i < (CACHE_STACK_SIZE - 1);i++)
		env->cacheStack[i] = 'A';

	//read and decrypt private key
	load_privatekey(env->privateKeyId,env->privateKey,KEY_LENGTH_IN_BYTES);
	//info("PrivateKey:");
	//info("%s",env->privateKey);

	//read input message
	for(i = 0;i < KEY_LENGTH_IN_BYTES;i++)
		tmp = env->in[i];	

	//read and write output
	for(i = 0;i < (KEY_LENGTH_IN_BYTES - 1);i++)
		env->out[i] = 'A';
	env->out[KEY_LENGTH_IN_BYTES - 1] = '\0';
	//info("Out :");
	//info("%s",env->out);	
}

/*
Crypto:
	1 crypto the message
	2 set env_clear = 1
*/
void crypto(void)
{
	cacheCryptoEnv env;
	unsigned long irq_flags;
	int flag2 = 1;
	unsigned int cpu = smp_processor_id();
	int flag1 = 1;

	//flush the cache before use cache as ram
	asm volatile("wbinvd\n\t");
	
	//swtich stack and call encrypt or decrypt
	strcpy(env.in,"Hello World!");
	env.privateKeyId = 0;
	fill_L1D(&env);	

	info("In :");
	info("%s",env.in);
		
	if(&env != NULL && private_key_compute != NULL && env.cacheStack != NULL) {
	//printk("&env:%lx  private_key_compute:%lx  env.cacheStack:%lx\n",&env ,private_key_compute,env.cacheStack + CACHE_STACK_SIZE - 4);
		switch_stack(&env,private_key_compute,env.cacheStack + CACHE_STACK_SIZE - 4);
	}	

	//private_key_compute(&env);

	//invd can't be used
	clear_env(&env);
		
	info("In :");
	info("%s",env.in);
	info("Out :");
	info("%s",env.out);

	cpu = smp_processor_id();
	info("Out :");
	info("%s",env.out);	
	
}

/*
thread_cryptogram:
	1 wait for thread_0 set cache_frozen = 1
	2 call crypto
*/
static int thread_cryptogram(void *arg)
{

	unsigned int cpu = 0;
	unsigned long irq_flags;
			
	env_clear = 0;	

	cpu = smp_processor_id();
	info("Trhead %d starts.",current->pid);

    start[cpu] = jiffies;
    if(cache_frozen == 0) {
		info("Cache hasn't been frozen. Wait for a minute");
		dur[cpu] = 0;
		while(cache_frozen == 0 && dur[cpu] <= 1000) {
		    dur[cpu] = jiffies_to_msecs(jiffies - start[cpu]);	    
		}        
    }
    
	preempt_disable();
	local_irq_save(irq_flags);  

    if(dur[cpu] < 1000) {
    	info("Start Crypto.");
    	crypto();
    }	 
	  
	//clear_env
	env_clear = 1;
	
	local_irq_restore(irq_flags);
	preempt_enable();	
	
	while(!kthread_should_stop()) {
		schedule_timeout_interruptible(msecs_to_jiffies(1));
	}


	return 0;
}

/*
	1 frozen cache
	2 if frozen_cache_time > WAIT_TIME 
		or thread_1 set env_clear = 1
		free the cache
*/
static int thread_frozen_cache(void *arg)
{
	unsigned int cpu = 0;
	unsigned long old_dur = 0;
	
	cpu = smp_processor_id();

	info("Thread %d starts.",current->pid);	

	//frozen cache
	if(isCacheWriteBack()) {
		info("Cache works on write-back Mode");
		enter_no_fill();
	} else {
		info("Cache  works on No-fill Mode already");
	}

	if(isCacheWriteBack() == 0) {
		info("Success!Cache enters no-fill mode.");
		cache_frozen = 1; 
	} else {
		info("Error! Failed to switch cache mode.");
	}

	//control cache frozen time
	start[cpu] = jiffies;
	
	if(env_clear == 0) {
		info("Data on other cache hasn't been cleared. Wait for a minute");
		dur[cpu] = 0;
		while(env_clear == 0 && dur[cpu] <= 1000) {
			old_dur = dur[cpu];
		    dur[cpu] = jiffies_to_msecs(jiffies - start[cpu]);
            if(dur[cpu] != old_dur) {
                info("Frozen cache for %d ms.\n",dur[cpu]);
            }		    
		}
	}
    if(dur[cpu] < 1000)
	    info("Data on other cache has been cleared.");
	else 
	    info("Frozen cache too long. Restore cr0.");    

	//free cache
	exit_no_fill();

	if(isCacheWriteBack() == 1) {
		info("Success!Cache gets back to normal mode.");
		cache_frozen = 0; 
	} else {
		info("Error! Failed to restore cache mode.");
	}

	while(!kthread_should_stop()) {
		schedule_timeout_interruptible(msecs_to_jiffies(2));// what is the best value of the time interval?
	}

	return 0;
}

/*
	car_init:
	1 create two thread
		(1) thread_frozen_cache on core0,
		(2) thread_cryptogram on core1
*/
static int __init car_init(void)
{
	int cpu = 0;
	

	unsigned int parameter[CORES];
	void * thread_func[CORES];
	
	thread_func[0] = thread_frozen_cache;
	thread_func[1] = thread_cryptogram;
	
	for(cpu = 0;cpu < CORES;cpu++) 
		parameter[cpu] = 0;
		
	for_each_present_cpu(cpu) {
		if(cpu < CORES) {
			parameter[cpu] = cpu;
			t_thread[cpu] = kthread_create(thread_func[cpu],(void *)(parameter+cpu),
							"trhead/%d",cpu);
			if(IS_ERR(t_thread[cpu])) {
				printk(KERN_ERR "[thread/%d]: creating kthread failed\n",cpu);
			
				goto out;
			}	
			kthread_bind(t_thread[cpu],cpu);
			wake_up_process(t_thread[cpu]);			
		}		
	}
	
	//schedule_timeout_interruptible(msecs_to_jiffies(sleep_millisecs));
			
out:
	return 0;					
}

/*
.       注意事项
（1）       在调用kthread_stop函数时，线程函数不能已经运行结束。否则，kthread_stop函数会一直进行等待。
（2）       线程函数必须能让出CPU，以便能运行其他线程。同时线程函数也必须能重新被调度运行。在程序中，这是通过schedule_timeout()函数完成的。
*/
static void __exit car_exit(void)// why i can't stop the threads in exit function/?
{
	int cpu = 0;
	
	for(cpu = 0;cpu < CORES;cpu++) 
		if(t_thread[cpu]) {
			printk("Stop thread on core %d\n",cpu);
			kthread_stop(t_thread[cpu]);
		}
}

module_init(car_init);
module_exit(car_exit);
MODULE_AUTHOR ("crane,zly");
MODULE_DESCRIPTION ("Cache as Ram.");
MODULE_LICENSE("GPL");
