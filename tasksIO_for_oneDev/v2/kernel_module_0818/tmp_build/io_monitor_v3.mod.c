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
	{ 0xc8dcc62a, "krealloc" },
	{ 0x5e8e2569, "vfs_getattr" },
	{ 0x8d522714, "__rcu_read_lock" },
	{ 0xe13e0c8c, "proc_create" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x37a0cba, "kfree" },
	{ 0x71ba2490, "pcpu_hot" },
	{ 0xeae94adb, "seq_lseek" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0x75743af2, "path_put" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0x122c3a7e, "_printk" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xa916b694, "strnlen" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0x718b0e91, "fput" },
	{ 0x8818b661, "pid_task" },
	{ 0x2469810f, "__rcu_read_unlock" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0x8e50f680, "kern_path" },
	{ 0xd1bd2ab6, "kernel_read" },
	{ 0x50ba7ec1, "__get_task_comm" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x1c161352, "param_ops_string" },
	{ 0x12f5299f, "seq_read" },
	{ 0xdd64e639, "strscpy" },
	{ 0x28aa6a67, "call_rcu" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xeaa78587, "filp_close" },
	{ 0x8772f0bc, "remove_proc_entry" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0x821bc71a, "seq_printf" },
	{ 0x3f66a26e, "register_kprobe" },
	{ 0x626c45d3, "seq_puts" },
	{ 0xd21b9cc9, "find_vpid" },
	{ 0xfa1f5da0, "single_release" },
	{ 0xbb10e61d, "unregister_kprobe" },
	{ 0x22e14f04, "kmalloc_trace" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x754d539c, "strlen" },
	{ 0x457d0285, "single_open" },
	{ 0x349cba85, "strchr" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x37a99944, "kmalloc_caches" },
	{ 0xb2c57545, "kernel_write" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xc6227e48, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "F322DDE4A677C9630F96D3C");
