#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>

static
int bcm2835_gpclk_init(void)
{
    struct device_node *node;
    struct platform_device *pdev;
    struct resource res;

    node = of_find_node_by_name(NULL, "cprman");
    if(!node) {
        printk(KERN_ERR "No cprman\n");
        return -ENODEV;
    }

    if((pdev=of_find_device_by_node(node))!=NULL) {
        dev_info(&pdev->dev, "Found cprman");


    }

    if(of_address_to_resource(node, 0, &res)) {
        printk(KERN_ERR "No cprman resource0\n");
        return -ENODEV;
    }

    printk(KERN_INFO "Found cprman @%llx\n", (unsigned long long)res.start);

    {
        int clkidx = 0;
        while(1) {
            struct clk *C;
            unsigned long rate;
            C = of_clk_get(node, clkidx);
            if(IS_ERR(C))
                break;

            rate = clk_get_rate(C);

            printk(KERN_INFO "cprman clock%d rate=%lu\n", clkidx, rate);

            clk_put(C);
            clkidx++;
        }
    }

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
