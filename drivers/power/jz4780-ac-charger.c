/*
 * ac-charger.c - General AC Charger driver.
 *
 * Copyright (C) 2013 Ingenic Semiconductor Co., Ltd.
 * Author: James Jia<ljia@ingenic.cn>.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jz4740-adc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/jz4780-ac-charger.h>
#include <linux/power/jz4780-battery.h>
#include <linux/slab.h>

struct jz4780_ac_charger {
    const struct jz4780_ac_charger_platform_data *pdata;
    unsigned int irq;
    struct delayed_work work;
    int charging;
    int status;
    struct power_supply *charger;
    struct power_supply_desc desc;
};

static char* status_dbg(int status)
{
    switch (status) {
    case POWER_SUPPLY_STATUS_UNKNOWN:
            return "UNKNOWN";
    case POWER_SUPPLY_STATUS_CHARGING:
            return "CHARGING";
    case POWER_SUPPLY_STATUS_DISCHARGING:
            return "DISCHARGING";
    case POWER_SUPPLY_STATUS_NOT_CHARGING:
            return "NOT_CHARGING";
    case POWER_SUPPLY_STATUS_FULL:
            return "FULL";
    default:
            return "ERROR";
    }
}

static int is_ac_online(const struct jz4780_ac_charger_platform_data *pdata)
{
    int tmp;

    tmp = gpio_get_value(pdata->gpio_ac);
    tmp ^= pdata->gpio_ac_active_low;

    return tmp;
}

static void update_battery(struct jz4780_ac_charger *ac, int status)
{
    struct power_supply *psy_battery = power_supply_get_by_name("battery");
    struct jz_battery *jz_battery = container_of(psy_battery->desc, struct jz_battery, desc);

    set_charger_offline(jz_battery, USB);
    if (status == POWER_SUPPLY_STATUS_NOT_CHARGING)
        set_charger_offline(jz_battery, AC);
    else
        set_charger_online(jz_battery, AC);
    jz_battery->status = status;
}

static void ac_work(struct work_struct *work)
{
    struct jz4780_ac_charger *ac;
    int status;
    int charging;

    ac = container_of(work, struct jz4780_ac_charger, work.work);
    charging = gpio_get_value(ac->pdata->gpio_charging) ^ ac->pdata->gpio_charging_active_low;
    pr_info("ac: ac_work: charging=%d \n", charging);
    if (charging != ac->charging) {
        status = charging ? POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_NOT_CHARGING;
        if (status == POWER_SUPPLY_STATUS_NOT_CHARGING && is_ac_online(ac->pdata))
            status = POWER_SUPPLY_STATUS_FULL;
        if (ac->status != status) {
            pr_info("ac: update status: %s -> %s\n",
            status_dbg(ac->status), status_dbg(status));
            ac->status = status;
            update_battery(ac, status);
        }

        ac->charging = charging;
    }

    power_supply_changed(ac->charger);
    enable_irq(ac->irq);
}

static irqreturn_t ac_irq(int irq, void *devid)
{
    struct jz4780_ac_charger *ac = devid;

    pr_info("ac: ac_irq\n");
    disable_irq_nosync(ac->irq);
    schedule_delayed_work(&ac->work, msecs_to_jiffies(200));
    return IRQ_HANDLED;
}

static inline struct jz4780_ac_charger *psy_to_ac(struct power_supply *psy)
{
    return container_of(psy->desc, struct jz4780_ac_charger, desc);
}

static int ac_get_property(struct power_supply *psy,
                           enum power_supply_property psp, union power_supply_propval *val)
{
    struct jz4780_ac_charger *ac = psy_to_ac(psy);

    switch (psp) {
    case POWER_SUPPLY_PROP_ONLINE:
            val->intval = is_ac_online(ac->pdata);
            break;
    default:
            return -EINVAL;
    }

    return 0;
}

static void ac_external_power_changed(struct power_supply *psy)
{
    struct jz4780_ac_charger *ac = psy_to_ac(psy);

    if (ac->status == POWER_SUPPLY_STATUS_FULL && !is_ac_online(ac->pdata)) {
        pr_info("ac: AC offline: FULL -> NOT_CHARGING\n");
        ac->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
        update_battery(ac, ac->status);
        power_supply_changed(ac->charger);
    } else
        pr_info("ac: AC changed (skip)\n");
}

static enum power_supply_property ac_properties[] = {
    POWER_SUPPLY_PROP_ONLINE,
};

static int get_pmu_status(void *pmu_interface, int status)
{
    struct jz4780_ac_charger *ac = (struct jz4780_ac_charger *)pmu_interface;

    switch (status) {
    case AC:
            return is_ac_online(ac->pdata);
    case USB:
            return 0;
    case STATUS:
            return ac->status;
    }
    return -1;
}

static void pmu_work_enable(void *pmu_interface)
{
    struct jz4780_ac_charger *ac = (struct jz4780_ac_charger *)pmu_interface;

    pr_info("ac: pmu_work_enable\n");
    schedule_delayed_work(&ac->work, msecs_to_jiffies(200));
}

static void ac_callback_init(struct jz4780_ac_charger *ac)
{
    struct power_supply *psy = power_supply_get_by_name("battery");
    struct jz_battery *jz_battery;
    jz_battery = container_of(psy->desc, struct jz_battery, desc);

    jz_battery->pmu_interface = ac;
    jz_battery->get_pmu_status = get_pmu_status;
    jz_battery->pmu_work_enable = pmu_work_enable;
}

static int jz4780_ac_charger_probe(struct platform_device *pdev)
{
    int irq;
    int ret;
    struct jz4780_ac_charger *ac;
    enum of_gpio_flags flags;
    struct device_node *np = pdev->dev.of_node;
    struct jz4780_ac_charger_platform_data *pdata;;

    pdata = kmalloc(sizeof(struct jz4780_ac_charger_platform_data), GFP_KERNEL);
    if (!pdata) {
        dev_err(&pdev->dev, "Failed to allocate driver data structre\n");
        ret = -ENOMEM;
        goto err_free_pdata;
    }

    if (of_property_read_string(np, "ac-name", &pdata->name)) {
        pdata->name = 0;
    }

    if (of_property_read_u32(np, "type", &pdata->type)) {
        dev_dbg(&pdev->dev, "Using default type POWER_SUPPLY_TYPE_MAINS\n");
        pdata->type = POWER_SUPPLY_TYPE_MAINS;
    }

    pdata->gpio_ac = of_get_named_gpio_flags(np, "gpio-ac", 0, &flags);
    if (gpio_is_valid(pdata->gpio_ac)) {
        pdata->gpio_ac_active_low = flags & OF_GPIO_ACTIVE_LOW;
    } else {
        dev_err(&pdev->dev, "Failed to get gpio-ac property\n");
        ret = -EINVAL;
        goto err_free_pdata;
    }

    pdata->gpio_charging = of_get_named_gpio_flags(np, "gpio-charging", 0, &flags);
    if (gpio_is_valid(pdata->gpio_charging)) {
        pdata->gpio_charging_active_low = flags & OF_GPIO_ACTIVE_LOW;
    } else {
        dev_err(&pdev->dev, "Failed to get gpio-charging property\n");
        ret = -EINVAL;
        goto err_free_pdata;
    }

    ac = kzalloc(sizeof(struct jz4780_ac_charger), GFP_KERNEL);
    if (!ac) {
        dev_err(&pdev->dev, "Failed to alloc driver structure\n");
        ret = -ENOMEM;
        goto err_free_pdata;
    }

    ac->desc.name = pdata->name ? pdata->name : "ac_charge";
    ac->desc.type = pdata->type;
    ac->desc.properties = ac_properties;
    ac->desc.num_properties = ARRAY_SIZE(ac_properties);
    ac->desc.get_property = ac_get_property;
    ac->desc.external_power_changed = ac_external_power_changed;

    ret = gpio_request(pdata->gpio_ac, dev_name(&pdev->dev));
    if (ret) {
        dev_err(&pdev->dev, "Failed to request gpio pin: %d\n", ret);
        goto err_free;
    }
    ret = gpio_direction_input(pdata->gpio_ac);
    if (ret) {
        dev_err(&pdev->dev, "Failed to set gpio to input: %d\n", ret);
        goto err_gpio_ac_free;
    }

    ret = gpio_request(pdata->gpio_charging, dev_name(&pdev->dev));
    if (ret) {
        dev_err(&pdev->dev, "Failed to request gpio pin: %d\n", ret);
        goto err_free;
    }
    ret = gpio_direction_input(pdata->gpio_charging);
    if (ret) {
        dev_err(&pdev->dev, "Failed to set gpio to input: %d\n", ret);
        goto err_gpio_free;
    }

    ac->pdata = pdata;
    ac->charging = -1;
    ac->status = POWER_SUPPLY_STATUS_UNKNOWN;

    ac->charger = power_supply_register(&pdev->dev, &ac->desc, NULL);
    if (!ac->charger) {
        dev_err(&pdev->dev, "Failed to register power supply: %d\n",
                ret);
        goto err_gpio_free;
    }

    INIT_DELAYED_WORK(&ac->work, ac_work);

    irq = gpio_to_irq(pdata->gpio_charging);
    if (irq > 0) {
        ret = request_any_context_irq(irq, ac_irq,
                                      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                                      dev_name(&pdev->dev), ac);
        if (ret)
            dev_warn(&pdev->dev, "Failed to request irq: %d\n", ret);
        else {
            ac->irq = irq;
            disable_irq_nosync(irq);
        }
    }

    ac_callback_init(ac);

    platform_set_drvdata(pdev, ac);

    return 0;

err_gpio_free:
    gpio_free(pdata->gpio_charging);
err_gpio_ac_free:
    gpio_free(pdata->gpio_ac);
err_free:
    kfree(ac);
err_free_pdata:
    kfree(pdata);
    return ret;
}

static int jz4780_ac_charger_remove(struct platform_device *pdev)
{
    struct jz4780_ac_charger *ac = platform_get_drvdata(pdev);

    if (ac->irq)
        free_irq(ac->irq, ac->charger);

    power_supply_unregister(ac->charger);

    gpio_free(ac->pdata->gpio_ac);
    gpio_free(ac->pdata->gpio_charging);

    platform_set_drvdata(pdev, NULL);
    kfree(ac);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int jz4780_ac_charger_resume(struct device *dev)
{
    return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(jz4780_ac_charger_pm_ops, NULL, jz4780_ac_charger_resume);

static struct of_device_id jz4780_of_match[] = {
        { .compatible = "ingenic,jz4780-charger", },
        { },
};
MODULE_DEVICE_TABLE(of, jz4780_of_match);

static struct platform_driver jz4780_ac_charger_driver = {
    .probe = jz4780_ac_charger_probe,
    .remove = jz4780_ac_charger_remove,
    .driver = {
        .name = "ac-charger",
        .owner = THIS_MODULE,
        .of_match_table = jz4780_of_match,
        .pm = &jz4780_ac_charger_pm_ops,
    },
};

static int jz4780_ac_charger_init(void)
{
    return platform_driver_register(&jz4780_ac_charger_driver);
}
module_init(jz4780_ac_charger_init);

static void jz4780_ac_charger_exit(void)
{
    platform_driver_unregister(&jz4780_ac_charger_driver);
}
module_exit(jz4780_ac_charger_exit);

MODULE_AUTHOR("James Jia <ljia@ingenic.cn>");
MODULE_DESCRIPTION("Driver for general AC charger");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ac-charger");
