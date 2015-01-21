#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x55e6e8b1, "module_layout" },
	{ 0xd0d8621b, "strlen" },
	{ 0xc0a3d105, "find_next_bit" },
	{ 0x47c7b0d2, "cpu_number" },
	{ 0x79c7d1ee, "kthread_create_on_node" },
	{ 0x7d11c268, "jiffies" },
	{ 0x10a30dc1, "kthread_bind" },
	{ 0xc805f93, "clflush_cache_range" },
	{ 0xfe7c4287, "nr_cpu_ids" },
	{ 0x30079626, "current_task" },
	{ 0x37befc70, "jiffies_to_msecs" },
	{ 0x50eedeb8, "printk" },
	{ 0x8e6294fd, "kthread_stop" },
	{ 0xb4390f9a, "mcount" },
	{ 0x815c56d0, "cpu_present_mask" },
	{ 0x8ff4079b, "pv_irq_ops" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x3bd1b1f6, "msecs_to_jiffies" },
	{ 0x9c40a126, "wake_up_process" },
	{ 0xd2965f6f, "kthread_should_stop" },
	{ 0x9c55cec, "schedule_timeout_interruptible" },
	{ 0xe914e41e, "strcpy" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "0C1496F2F89D28A9AC29621");
