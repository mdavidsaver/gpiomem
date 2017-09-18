#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

static
int bcm2835_gpclk_init(void)
{
    struct device_node *node;
    struct resource res;

    node = of_find_node_by_name(NULL, "cprman");
    if(!node) {
        printk(KERN_ERR "No cprman\n");
        return -ENODEV;
    }

    if(of_address_to_resource(node, 0, &res)) {
        printk(KERN_ERR "No cprman resource0\n");
        return -ENODEV;
    }

    printk("Found cprman @%llx\n", (unsigned long long)res.start);

    return 0;
}
module_init(bcm2835_gpclk_init)

static
void bcm2835_gpclk_remove(void)
{

}
module_exit(bcm2835_gpclk_remove)

MODULE_AUTHOR("Michael Davidsaver <mdavidsaver@gmail.com>");
MODULE_DESCRIPTION("BCM2835 GPCLK monkey patching");
MODULE_LICENSE("GPL v2");
