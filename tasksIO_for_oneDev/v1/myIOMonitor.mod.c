#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x64f48f75, "filp_open" },
	{ 0x8d522714, "__rcu_read_lock" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0xe13e0c8c, "proc_create" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x69acdf38, "memcpy" },
	{ 0x37a0cba, "kfree" },
	{ 0xeae94adb, "seq_lseek" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x122c3a7e, "_printk" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xa916b694, "strnlen" },
	{ 0x3f068993, "init_task" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0x2469810f, "__rcu_read_unlock" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x9166fada, "strncpy" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0xd1bd2ab6, "kernel_read" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x1c161352, "param_ops_string" },
	{ 0x34c7cdbc, "lookup_bdev" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x12f5299f, "seq_read" },
	{ 0xdd64e639, "strscpy" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xeaa78587, "filp_close" },
	{ 0x8772f0bc, "remove_proc_entry" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x821bc71a, "seq_printf" },
	{ 0x899b173a, "vfs_llseek" },
	{ 0x626c45d3, "seq_puts" },
	{ 0xfa1f5da0, "single_release" },
	{ 0x22e14f04, "kmalloc_trace" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xf6008d28, "param_ops_int" },
	{ 0x457d0285, "single_open" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x37a99944, "kmalloc_caches" },
	{ 0xb2c57545, "kernel_write" },
	{ 0xc6227e48, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "FDA2A70274ED42928241500");
