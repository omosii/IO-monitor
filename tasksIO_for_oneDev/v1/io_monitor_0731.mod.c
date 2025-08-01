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
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x122c3a7e, "_printk" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0x8772f0bc, "remove_proc_entry" },
	{ 0xb43f9365, "ktime_get" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x626c45d3, "seq_puts" },
	{ 0x821bc71a, "seq_printf" },
	{ 0x8d522714, "__rcu_read_lock" },
	{ 0x3f068993, "init_task" },
	{ 0x2469810f, "__rcu_read_unlock" },
	{ 0xf9a482f9, "msleep" },
	{ 0x9166fada, "strncpy" },
	{ 0xa916b694, "strnlen" },
	{ 0x34c7cdbc, "lookup_bdev" },
	{ 0xe13e0c8c, "proc_create" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0x12f5299f, "seq_read" },
	{ 0xeae94adb, "seq_lseek" },
	{ 0xfa1f5da0, "single_release" },
	{ 0x1c161352, "param_ops_string" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x457d0285, "single_open" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0x37a0cba, "kfree" },
	{ 0xc6227e48, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "85E8115E5F1787AA47C94B6");
