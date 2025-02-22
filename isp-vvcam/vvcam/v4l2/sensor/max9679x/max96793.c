// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, Framos. All rights reserved.
 *
 * max96793.c - max96793 GMSL Serializer driver
 */
//#define DEBUG 1
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#include "max96793.h"

/* register specifics */
#define MAX96793_MIPI_RX0_ADDR 0x330
#define MAX96793_MIPI_RX1_ADDR 0x331
#define MAX96793_MIPI_RX2_ADDR 0x332
#define MAX96793_MIPI_RX3_ADDR 0x333

#define MAX96793_PIPE_Z_DT_ADDR 0x318

#define max96793_CTRL0_ADDR 0x10

/* RST_0 */
#define MAX96793_GPIO0_A 0x2BE
#define MAX96793_GPIO0_B 0x2BF
#define MAX96793_GPIO0_C 0x2C0
/* MFP1_CFG0 */
#define MAX96793_GPIO1_A 0x2C1
#define MAX96793_GPIO1_B 0x2C2
#define MAX96793_GPIO1_C 0x2C3
/* MFP2_CFG1 */
#define MAX96793_GPIO2_A 0x2C4
#define MAX96793_GPIO2_B 0x2C5
#define MAX96793_GPIO2_C 0x2C6
/* GPIO1_XVS0 */
#define MAX96793_GPIO3_A 0x2C7
#define MAX96793_GPIO3_B 0x2C8
#define MAX96793_GPIO3_C 0x2C9
/* MCLK_PLL */
#define MAX96793_GPIO4_A 0x2CA
#define MAX96793_GPIO4_B 0x2CB
#define MAX96793_GPIO4_C 0x2CC
/* GPIO3_XTRIG0 */
#define MAX96793_GPIO5_A 0x2CD
#define MAX96793_GPIO5_B 0x2CE
#define MAX96793_GPIO5_C 0x2CF
/* GPIO10_XTRIG1 */
#define MAX96793_GPIO6_A 0x2D0
#define MAX96793_GPIO6_B 0x2D1
#define MAX96793_GPIO6_C 0x2D2
/* GPIO2_XHS0 */
#define MAX96793_GPIO7_A 0x2D3
#define MAX96793_GPIO7_B 0x2D4
#define MAX96793_GPIO7_C 0x2D5
/* PW_EN_0 */
#define MAX96793_GPIO8_A 0x2D6
#define MAX96793_GPIO8_B 0x2D7
#define MAX96793_GPIO8_C 0x2D8
/* I2C_SDA */
#define MAX96793_GPIO9_A 0x2D9
#define MAX96793_GPIO9_B 0x2DA
#define MAX96793_GPIO9_C 0x2DB
/* I2C_SCL */
#define MAX96793_GPIO10_A 0x2DC
#define MAX96793_GPIO10_B 0x2DD
#define MAX96793_GPIO10_C 0x2DE

#define MAX96793_REF_VTG0 0x3F0 //select MCLK output frequency value
#define MAX96793_REF_VTG1 0x3F1 //select MCLK output GPIO pin

#define MAX96793_START_PORTBZ_ADDR 0x311
#define MAX96793_ENABLE_PORTBZ_ADDR 0x02
#define MAX96793_CSI_PORT_SEL_ADDR 0x308

#define MAX96793_I2C2_ADDR 0x42
#define MAX96793_I2C3_ADDR 0x43
#define MAX96793_I2C4_ADDR 0x44
#define MAX96793_I2C5_ADDR 0x45

#define MAX96793_DEV_ADDR 0x00

#define MAX96793_CSI_MODE_1X4 0x00

#define MAX96793_CSI_PORT_B(num_lanes) (((num_lanes) << 4) & 0xF0)

#define MAX96793_CSI_1X4_MODE_LANE_MAP1 0xE0
#define MAX96793_CSI_1X4_MODE_LANE_MAP2 0x04

#define MAX96793_ST_ID_0 0x0
#define MAX96793_ST_ID_1 0x1
#define MAX96793_ST_ID_2 0x2
#define MAX96793_ST_ID_3 0x3

#define MAX96793_PIPE_Z_START_B 0x40

#define MAX96793_START_PORT_A 0x10
#define MAX96793_START_PORT_B 0x20

#define MAX96793_CSI_1_LANE 0
#define MAX96793_CSI_2_LANE 1
#define MAX96793_CSI_3_LANE 2
#define MAX96793_CSI_4_LANE 3

#define MAX96793_EN_LINE_INFO 0x40

#define MAX96793_VID_TX_EN_Z 0x40


#define MAX96793_VID_INIT 0x3
#define MAX96793_SRC_RCLK 0x89

#define MAX96793_RESET_ALL 0x80
#define MAX96793_RESET_SRC 0x60
#define MAX96793_PWDN_GPIO 0x90

#define MAX96793_MAX_PIPES 0x4
#define MAX96793_MAX_RETRIES 1000

/*register flags*/
#define GPIO_OUT_DIS	0x01
#define GPIO_TX_EN		(0x01 << 1)
#define GPIO_RX_EN		(0x01 << 2)

struct max96793_client_ctx {
	struct gmsl_link_ctx *g_ctx;
	bool st_done;
};

struct max96793 {
	struct i2c_client *i2c_client;
	struct regmap *regmap;
	struct max96793_client_ctx g_client;
	struct mutex lock;
	/* primary serializer properties */
	__u32 def_addr;
	__u32 pst2_ref;
};

static struct max96793 *prim_priv__;

struct map_ctx {
	u8 dt;
	u16 addr;
	u8 val;
	u8 st_id;
};

static int max96793_write_reg(struct device *dev, u16 addr, u8 val)
{
	struct max96793 *priv = dev_get_drvdata(dev);
	int err;
	int num_retry = 0;

	for (num_retry = 0; num_retry < MAX96793_MAX_RETRIES; num_retry++) {
		err = regmap_write(priv->regmap, addr, val);
		if (err >= 0)
			break;
		usleep_range(1000, 1100);
	}

	if (err < 0) {
		dev_err(dev, "Write reg error: reg=%x, val=%x, error= %d after %d retries\n",
			addr, val, err, num_retry);
		return err;
	}

	dev_dbg(dev, "Succesfully writen reg : reg=%x, val=%xd\n", addr, val);

	if (num_retry > 0)
		dev_warn(dev, "i2c communication passed after %d retries: reg=%x",
			num_retry, addr);

	/* delay before next i2c command as required for SERDES link */
	usleep_range(100, 110);

	return err;
}

int max96793_gmsl3_setup(struct device *dev)
{
	struct max96793 *priv = dev_get_drvdata(dev);
	int err = 0;

	mutex_lock(&priv->lock);
	dev_dbg(dev, "enter %s function\n", __func__);
	max96793_write_reg(dev, 0x577, 0x7F); //Enable independent resets for links A and B

	max96793_write_reg(dev, 0x14CE, 0x19); //Enable SION - ERRATA
	max96793_write_reg(dev, 0x01, 0x0C); // 12 Gbps
	max96793_write_reg(dev, 0x06, 0x11);
	max96793_write_reg(dev, 0x28, 0x62); //FEC on
	msleep(100);

	err = max96793_write_reg(dev, max96793_CTRL0_ADDR, 0x21);
	msleep(100);

	if (err)
		dev_err(dev, "gmsl3 config failed!\n");

	mutex_unlock(&priv->lock);

	return err;
}
EXPORT_SYMBOL(max96793_gmsl3_setup);

int max96793_setup_streaming(struct device *dev, u32 code)
{
	struct max96793 *priv = dev_get_drvdata(dev);
	int err = 0;
	//u32 csi_mode;
	u32 lane_map1;
	u32 lane_map2;
	u32 port;
	u32 rx1_lanes = 0;
	u32 port_sel = 0;
	struct gmsl_link_ctx *g_ctx;
	u32 i;

	dev_dbg(dev,
				"%s: ++\n",
				__func__);

	priv->g_client.st_done = false;

	mutex_lock(&priv->lock);

	if (!priv->g_client.g_ctx) {
		dev_err(dev, "%s: no sdev client found\n", __func__);
		err = -EINVAL;
		goto error;
	}

	if (priv->g_client.st_done) {
		dev_dbg(dev, "%s: stream setup is already done\n", __func__);
		goto error;
	}

	g_ctx = priv->g_client.g_ctx;

	//reset mipi
	max96793_write_reg(dev, MAX96793_MIPI_RX0_ADDR, 0x08);
	max96793_write_reg(dev, MAX96793_MIPI_RX0_ADDR, 0x00);

	//csi_mode = MAX96793_CSI_MODE_1X4;
	lane_map1 = MAX96793_CSI_1X4_MODE_LANE_MAP1;
	lane_map2 = MAX96793_CSI_1X4_MODE_LANE_MAP2;
	rx1_lanes = g_ctx->num_csi_lanes-1;

	port = MAX96793_CSI_PORT_B(rx1_lanes);

	//max96793_write_reg(dev, MAX96793_MIPI_RX0_ADDR, csi_mode);
	max96793_write_reg(dev, MAX96793_MIPI_RX1_ADDR, (port | 0x40)); //deskew on
	max96793_write_reg(dev, MAX96793_MIPI_RX2_ADDR, lane_map1);
	max96793_write_reg(dev, MAX96793_MIPI_RX3_ADDR, lane_map2);

	for (i = 0; i < g_ctx->num_streams; i++)
		if (g_ctx->streams[i].st_id_sel != GMSL_ST_ID_UNUSED)
			port_sel |= (1 << g_ctx->streams[i].st_id_sel);

	if (code == MEDIA_BUS_FMT_SRGGB10_1X10
		|| code == MEDIA_BUS_FMT_SGBRG10_1X10) {
		max96793_write_reg(dev, 0x31E, 0x2A);	// software override bpp on pipe Z
		max96793_write_reg(dev, 0x111, 0x4A);	// BPP = 10
		dev_dbg(dev, "%s: 10 bpp\n", __func__);

	} else if (code == MEDIA_BUS_FMT_SRGGB12_1X12
		|| code == MEDIA_BUS_FMT_SGBRG12_1X12){
		max96793_write_reg(dev, 0x31E, 0x2C);	// software override bpp on pipe Z
		max96793_write_reg(dev, 0x111, 0x4C);	// BPP = 12
		dev_dbg(dev, "%s: 12 bpp\n", __func__);
	}

	max96793_write_reg(dev, 0x312, 0x04);	// Double EMB8 on pipe Z
	max96793_write_reg(dev, 0x110, 0x28);	// Disable AUTO_BPP
	max96793_write_reg(dev, 0x112, 0x0A);	// limit heart

	if (g_ctx->dst_vc == 1)
		max96793_write_reg(dev, 0x5B, 0x02); // Pipe Z stream ID
	else
		max96793_write_reg(dev, 0x5B, 0x01); // Pipe Z stream ID

	max96793_write_reg(dev, 0x383, 0x80); // tunneling mode for gmsl2 and gmsl3

	max96793_write_reg(dev, MAX96793_START_PORTBZ_ADDR, 0x40); // start video
	max96793_write_reg(dev, MAX96793_CSI_PORT_SEL_ADDR, 0x64); // enable CSI on port B
	max96793_write_reg(dev, MAX96793_ENABLE_PORTBZ_ADDR, 0x43); // Select port B for pipe Z

	priv->g_client.st_done = true;

error:
	mutex_unlock(&priv->lock);
	return err;
}
EXPORT_SYMBOL(max96793_setup_streaming);

int max96793_setup_control(struct device *dev)
{
	struct max96793 *priv = dev_get_drvdata(dev);
	int err = 0;
	struct gmsl_link_ctx *g_ctx;
	//u8 reg;

	mutex_lock(&priv->lock);

	if (!priv->g_client.g_ctx) {
		dev_err(dev, "%s: no sensor dev client found\n", __func__);
		err = -EINVAL;
		goto error;
	}

	g_ctx = priv->g_client.g_ctx;

	if (g_ctx->serdes_csi_link == GMSL_SERDES_CSI_LINK_A) {
		err = max96793_write_reg(dev, max96793_CTRL0_ADDR, 0x21);
		dev_dbg(dev, "%s: reset one shot serializer\n", __func__);

	} else {
		err = max96793_write_reg(dev, max96793_CTRL0_ADDR, 0x22);
	}
	/* check if serializer device exists */
	if (err) {
		dev_err(dev, "%s: ERROR: ser device not found\n", __func__);
		goto error;
	}

	/* delay to settle link */
	msleep(100);

	err = max96793_write_reg(dev, 0x40, 0x16); // i2c 400 kHz
	if (err)
		dev_err(dev, "error setting i2c speed\n");

	/* dev addr pass-through2 ref */
	prim_priv__->pst2_ref++;

	/* RST_0 */
	err = max96793_write_reg(dev, MAX96793_GPIO0_A, 0x80 | GPIO_RX_EN); // pull up resistor value 1Mohm; enable gpio
	if (err)
		dev_err(dev, "error setting MAX96793_GPIO0_A\n");
	err = max96793_write_reg(dev, MAX96793_GPIO0_C, 0x4F); // RX_ID=15
	if (err)
		dev_err(dev, "error setting MAX96793_GPIO0_C\n");

	/* XVS0 */
	if (err)
		goto error;
	dev_dbg(dev,
				"%s: Serializer MFP0 config done\n",
				__func__);

	err = max96793_write_reg(dev, MAX96793_GPIO8_A, 0x80 | 0x10); // pull up resistor value 1Mohm; enable gpio
	dev_dbg(dev,
				"%s: PW_EN0/TENABLE config done\n",
				__func__);

	g_ctx->serdev_found = true;

error:
	mutex_unlock(&priv->lock);
	return err;
}
EXPORT_SYMBOL(max96793_setup_control);


int max96793_xvs_setup(struct device *dev, bool direction)
{
	struct max96793 *priv = dev_get_drvdata(dev);
	int err = 0;

	mutex_lock(&priv->lock);

	//Serializer MFP3 - XVS0 config
	if (direction == max96793_OUT) {
		err = max96793_write_reg(dev, MAX96793_GPIO3_A, 0x80 | GPIO_RX_EN); // pull up resistor value 1Mohm; enable gpio
		err |= max96793_write_reg(dev, MAX96793_GPIO3_B, 0xA3); // default value
		err |= max96793_write_reg(dev, MAX96793_GPIO3_C, 0x50); // RX_ID=16
	} else {
		err = max96793_write_reg(dev, MAX96793_GPIO3_A, 0x80 | GPIO_TX_EN); // pull up resistor value 1Mohm; enable gpio
		err |= max96793_write_reg(dev, MAX96793_GPIO3_B, 0x10); // TX_ID=16
		err |= max96793_write_reg(dev, MAX96793_GPIO3_C, 0x43); // default value

	}

	if (err) {
		dev_err(dev,
			"%s: max96793 xvs ERR\n", __func__);
	}

	mutex_unlock(&priv->lock);
	return err;
}
EXPORT_SYMBOL(max96793_xvs_setup);

int max96793_gpio10_xtrig1_setup(struct device *dev, char *image_sensor_type)
{
	int err = 0;

	if ((image_sensor_type[0] == 's' && image_sensor_type[1] == 'l' && image_sensor_type[2] == 'v' && image_sensor_type[3] == 's') ||
	(image_sensor_type[0] == 'l' && image_sensor_type[1] == 'v' && image_sensor_type[2] == 'd' && image_sensor_type[3] == 's')) {
		//input (on lvds/slvs sensors)
		err = max96793_write_reg(dev, MAX96793_GPIO6_A, 0x81);
		err |= max96793_write_reg(dev, MAX96793_GPIO6_B, 0x06);

	} else {
		err = max96793_write_reg(dev, MAX96793_GPIO6_A, 0x80); // pull up resistor value 1Mohm; enable gpio
	}

	if (err) {
		dev_err(dev, "%s: ERROR: gpio10/xtrig1 config failed!\n", __func__);
		return err;
	}
	dev_dbg(dev,
				"%s: gpio10/xtrig1 config done\n",
				__func__);

	return 0;
}
EXPORT_SYMBOL(max96793_gpio10_xtrig1_setup);

int max96793_reset_control(struct device *dev)
{
	struct max96793 *priv = dev_get_drvdata(dev);
	int err = 0;

	mutex_lock(&priv->lock);
	if (!priv->g_client.g_ctx) {
		dev_err(dev, "%s: no sdev client found\n", __func__);
		err = -EINVAL;
		goto error;
	}

	prim_priv__->pst2_ref--;
	priv->g_client.st_done = false;

	max96793_write_reg(dev, MAX96793_DEV_ADDR, (prim_priv__->def_addr << 1));

	//max96793_write_reg(&prim_priv__->i2c_client->dev, max96793_CTRL0_ADDR, MAX96793_RESET_ALL);
	max96793_write_reg(dev, max96793_CTRL0_ADDR, MAX96793_RESET_ALL);

	msleep(100);

error:
	mutex_unlock(&priv->lock);
	return err;
}
EXPORT_SYMBOL(max96793_reset_control);

int max96793_sdev_pair(struct device *dev, struct gmsl_link_ctx *g_ctx)
{
	struct max96793 *priv;
	int err = 0;

	if (!dev || !g_ctx || !g_ctx->s_dev) {
		dev_err(dev, "%s: invalid input params\n", __func__);
		return -EINVAL;
	}

	priv = dev_get_drvdata(dev);
	mutex_lock(&priv->lock);
	if (priv->g_client.g_ctx) {
		dev_err(dev, "%s: device already paired\n", __func__);
		err = -EINVAL;
		goto error;
	}

	priv->g_client.st_done = false;

	priv->g_client.g_ctx = g_ctx;

error:
	mutex_unlock(&priv->lock);
	return 0;
}
EXPORT_SYMBOL(max96793_sdev_pair);

int max96793_sdev_unpair(struct device *dev, struct device *s_dev)
{
	struct max96793 *priv = NULL;
	int err = 0;

	if (!dev || !s_dev) {
		dev_err(dev, "%s: invalid input params\n", __func__);
		return -EINVAL;
	}

	priv = dev_get_drvdata(dev);
	mutex_lock(&priv->lock);

	if (!priv->g_client.g_ctx) {
		dev_err(dev, "%s: device is not paired\n", __func__);
		err = -ENOMEM;
		goto error;
	}

	if (priv->g_client.g_ctx->s_dev != s_dev) {
		dev_err(dev, "%s: invalid device\n", __func__);
		err = -EINVAL;
		goto error;
	}

	priv->g_client.g_ctx = NULL;
	priv->g_client.st_done = false;

error:
	mutex_unlock(&priv->lock);
	return err;
}
EXPORT_SYMBOL(max96793_sdev_unpair);

static struct regmap_config max96793_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

static int max96793_probe(struct i2c_client *client)
{
	struct max96793 *priv;
	int err = 0;
	struct device_node *node = client->dev.of_node;

	dev_info(&client->dev, "[max96793]: probing GMSL Serializer\n");

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	priv->i2c_client = client;
	priv->regmap = devm_regmap_init_i2c(priv->i2c_client,
				&max96793_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	mutex_init(&priv->lock);
	if (of_get_property(node, "is-prim-ser", NULL)) {
		if (prim_priv__) {
			dev_err(&client->dev,
				"prim-ser already exists\n");
				//return -EEXIST;
		}

		err = of_property_read_u32(node, "reg", &priv->def_addr);
		if (err < 0) {
			dev_err(&client->dev, "reg not found\n");
			return -EINVAL;
		}

		prim_priv__ = priv;
	}

	dev_set_drvdata(&client->dev, priv);

	/* dev communication gets validated when GMSL link setup is done */
	dev_info(&client->dev, "%s: success\n", __func__);

	return err;
}

static void max96793_remove(struct i2c_client *client)
{
	struct max96793 *priv;

	dev_info(&client->dev, "%s: Removing 96793\n", __func__);

	if (client != NULL) {
		priv = dev_get_drvdata(&client->dev);
		devm_kfree(&client->dev, priv);
		mutex_destroy(&priv->lock);
		client = NULL;
	}

}

static const struct i2c_device_id max96793_id[] = {
	{ "max96793", 0 },
	{ },
};

const struct of_device_id max96793_of_match[] = {
	{ .compatible = "framos,max96793", },
	{ },
};
MODULE_DEVICE_TABLE(of, max96793_of_match);
MODULE_DEVICE_TABLE(i2c, max96793_id);

static struct i2c_driver max96793_i2c_driver = {
	.driver = {
		.name = "max96793",
		.owner = THIS_MODULE,
	},
	.probe = max96793_probe,
	.remove = max96793_remove,
	.id_table = max96793_id,
};

static int __init max96793_init(void)
{
	return i2c_add_driver(&max96793_i2c_driver);
}

static void __exit max96793_exit(void)
{
	i2c_del_driver(&max96793_i2c_driver);
}

module_init(max96793_init);
module_exit(max96793_exit);

MODULE_DESCRIPTION("GMSL Serializer driver for max96793");
MODULE_AUTHOR("FRAMOS GmbH");
MODULE_LICENSE("GPL v2");
