#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/concat.h>

#define MTD_PART_DATA_1			"Data_1"
#define MTD_PART_DATA_2			"Data_2"

#define MTD_DATA_MAP			"ndmdatapart"
#define MTD_UBI_MAP				"ndmubipart"

static struct mtd_info *mymtd[2];
static struct mtd_info *merged_mtd;

enum part {
	PART_DATA_FULL,
	PART_MAX
};

struct part_dsc {
	const char *name;
	uint32_t offset;
	uint32_t size;
};

static struct part_dsc parts[PART_MAX] = {
	[PART_DATA_FULL] = {
		name: "Data_Full",
		offset: 0,
		size: MTDPART_SIZ_FULL
	}
};

static int create_data_mtd_partitions(struct mtd_info *m,
				 struct mtd_partition **pparts,
				 struct mtd_part_parser_data *data)
{
	struct mtd_partition *ndm_data_parts;
	const unsigned ndm_data_parts_num = 1;
	size_t i;

	ndm_data_parts = kzalloc(
		sizeof(*ndm_data_parts) * ndm_data_parts_num, GFP_KERNEL);

	if (ndm_data_parts == NULL)
		return -ENOMEM;

	for (i = 0; i < PART_MAX; i++) {
		ndm_data_parts[i].name = (char *)parts[i].name;
		ndm_data_parts[i].offset = parts[i].offset;
		ndm_data_parts[i].size = parts[i].size;
	}

	*pparts = ndm_data_parts;

	return ndm_data_parts_num;
}

static struct mtd_part_parser ndm_data_parser = {
	.owner = THIS_MODULE,
	.parse_fn = create_data_mtd_partitions,
	.name = MTD_DATA_MAP,
};

static const char *upart_probe_types[] = {
	MTD_UBI_MAP,
	NULL
};

enum upart {
	UPART_UBI_FULL,
	UPART_MAX
};

struct upart_dsc {
	const char *name;
	uint32_t offset;
	uint32_t size;
};

static struct upart_dsc uparts[UPART_MAX] = {
	[UPART_UBI_FULL] = {
		name: "UBI_Full",
		offset: 0,
		size: MTDPART_SIZ_FULL
	}
};

static int create_ubi_mtd_partitions(struct mtd_info *m,
				 struct mtd_partition **pparts,
				 struct mtd_part_parser_data *data)
{
	struct mtd_partition *ndm_ubi_parts;
	const unsigned ndm_ubi_parts_num = 1;
	size_t i;

	ndm_ubi_parts = kzalloc(
		sizeof(*ndm_ubi_parts) * ndm_ubi_parts_num, GFP_KERNEL);

	if (ndm_ubi_parts == NULL)
		return -ENOMEM;

	for (i = 0; i < UPART_MAX; i++) {
		ndm_ubi_parts[i].name = (char *)uparts[i].name;
		ndm_ubi_parts[i].offset = uparts[i].offset;
		ndm_ubi_parts[i].size = uparts[i].size;
	}

	*pparts = ndm_ubi_parts;

	return ndm_ubi_parts_num;
}

static struct mtd_part_parser ndm_ubi_parser = {
	.owner = THIS_MODULE,
	.parse_fn = create_ubi_mtd_partitions,
	.name = MTD_UBI_MAP,
};

static int __init init_ndm_data_concat(void)
{
	struct mtd_info *data_1_mtd = NULL;
	struct mtd_info *data_2_mtd = NULL;
	int err = 0;

	memset(mymtd, 0, sizeof(mymtd));

	pr_info("Searching for suitable data partitions...\n");

	data_1_mtd = get_mtd_device_nm(MTD_PART_DATA_1);
	if (IS_ERR(data_1_mtd)) {
		pr_info("No data partitions found\n");

		return err;
	}

	pr_info("Found 1st data partition of size %llu bytes\n", data_1_mtd->size);

	mymtd[0] = data_1_mtd;

	data_2_mtd = get_mtd_device_nm(MTD_PART_DATA_2);
	if (!IS_ERR(data_2_mtd)) {
		pr_info("Found 2nd data partition of size %llu bytes\n", data_2_mtd->size);
		mymtd[1] = data_2_mtd;
	}

	pr_info("Registering UBI data partitions parser\n");

	register_mtd_parser(&ndm_ubi_parser);

	pr_info("Registering NDM data partitions parser\n");

	register_mtd_parser(&ndm_data_parser);

	/* Combine the two partitions into a single MTD device & register it: */
	merged_mtd = mtd_concat_create(mymtd,
		mymtd[1] == NULL ? 1 : 2,
		"NDM combined UBI partition");
	if (merged_mtd)
		err = mtd_device_parse_register(
			merged_mtd, upart_probe_types, NULL, NULL, 0);

	if (!err)
		pr_info("Merging data partitions OK\n");
	else
		pr_err("Merging data partitions failed\n");

	return err;
}

static void __exit cleanup_ndm_data_concat(void)
{
	if (merged_mtd) {
		mtd_device_unregister(merged_mtd);
		mtd_concat_destroy(merged_mtd);
	}

	if (mymtd[1])
		put_mtd_device(mymtd[1]);

	if (mymtd[0])
		put_mtd_device(mymtd[0]);
}

module_init(init_ndm_data_concat);
module_exit(cleanup_ndm_data_concat);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NDM");
MODULE_DESCRIPTION("MTD Data map driver for Keeentic");
