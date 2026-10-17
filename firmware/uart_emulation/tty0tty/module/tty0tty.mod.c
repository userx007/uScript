#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

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



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x2f931da6, "tty_port_init" },
	{ 0x93ed645b, "tty_port_link_device" },
	{ 0xedf154c1, "tty_register_driver" },
	{ 0x92997ed8, "_printk" },
	{ 0x74ed20a2, "tty_driver_kref_put" },
	{ 0xf02230ed, "kmalloc_caches" },
	{ 0xccd31679, "__kmalloc_cache_noprof" },
	{ 0xe5b24c0, "tty_register_device_attr" },
	{ 0x9f40b742, "tty_unregister_device" },
	{ 0x2d450765, "__tty_insert_flip_string_flags" },
	{ 0xd1855e55, "tty_flip_buffer_push" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0xe5061253, "const_pcpu_hot" },
	{ 0xaad8c7d6, "default_wake_function" },
	{ 0x4afb2238, "add_wait_queue" },
	{ 0x1000e51, "schedule" },
	{ 0x37110088, "remove_wait_queue" },
	{ 0x6383b27c, "__x86_indirect_thunk_rdx" },
	{ 0x6f537bb3, "tty_unthrottle" },
	{ 0xb7ac0663, "tty_driver_flush_buffer" },
	{ 0xe2964344, "__wake_up" },
	{ 0xb557f8a4, "tty_port_destroy" },
	{ 0x3d93923f, "tty_unregister_driver" },
	{ 0x37a0cba, "kfree" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x6626afca, "down" },
	{ 0xcf2a6966, "up" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0xbd394d8, "tty_termios_baud_rate" },
	{ 0x2ce22b5a, "sysfs_notify" },
	{ 0xd39c4bcb, "tty_check_change" },
	{ 0x668b19a1, "down_read" },
	{ 0x53b954a2, "up_read" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x36cce78, "tty_termios_input_baud_rate" },
	{ 0xac9e3982, "tty_set_termios" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x763f94ae, "__tty_alloc_driver" },
	{ 0x67b27ec1, "tty_std_termios" },
	{ 0x7fe2a4c3, "module_layout" },
};

MODULE_INFO(depends, "");

