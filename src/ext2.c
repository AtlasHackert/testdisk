/*

    File: ext2.c

    Copyright (C) 1998-2008 Christophe GRENIER <grenier@cgsecurity.org>
  
    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
  
    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
 
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "types.h"
#include "common.h"
#include "ext2.h"
#include "fnctdsk.h"
#include "log.h"
#include "guid_cpy.h"

static int set_EXT2_info(const struct ext2_super_block *sb, partition_t *partition, const int verbose);
static int test_EXT2(const struct ext2_super_block *sb, partition_t *partition);

int check_EXT2(disk_t *disk_car,partition_t *partition,const int verbose)
{
  unsigned char *buffer=(unsigned char*)MALLOC(EXT2_SUPERBLOCK_SIZE);
  if(disk_car->pread(disk_car, buffer, EXT2_SUPERBLOCK_SIZE, partition->part_offset + 0x400) != EXT2_SUPERBLOCK_SIZE)
  {
    free(buffer);
    return 1;
  }
  if(test_EXT2((struct ext2_super_block*)buffer, partition)!=0)
  {
    free(buffer);
    return 1;
  }
  set_EXT2_info((struct ext2_super_block*)buffer, partition, verbose);
  free(buffer);
  return 0;
}

static int set_EXT2_info(const struct ext2_super_block *sb, partition_t *partition, const int verbose)
{
  partition->blocksize=EXT2_MIN_BLOCK_SIZE<<le32(sb->s_log_block_size);
  set_part_name(partition,sb->s_volume_name,16);
  /* sb->s_last_mounted seems to be unemployed in kernel 2.2.16 */
  if(EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_HUGE_FILE)!=0 ||
      EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_GDT_CSUM)!=0 ||
      EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_DIR_NLINK)!=0 ||
      EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE)!=0 ||
      EXT2_HAS_INCOMPAT_FEATURE(sb,EXT4_FEATURE_INCOMPAT_64BIT)!=0 ||
      EXT2_HAS_INCOMPAT_FEATURE(sb,EXT4_FEATURE_INCOMPAT_MMP)!=0)
    snprintf(partition->info, sizeof(partition->info), "ext4 blocksize=%u", partition->blocksize);
  else if(EXT2_HAS_COMPAT_FEATURE(sb,EXT3_FEATURE_COMPAT_HAS_JOURNAL)!=0)
    snprintf(partition->info, sizeof(partition->info), "ext3 blocksize=%u", partition->blocksize);
  else
    snprintf(partition->info, sizeof(partition->info), "ext2 blocksize=%u", partition->blocksize);
  if(EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT2_FEATURE_RO_COMPAT_LARGE_FILE)!=0)
    strcat(partition->info," Large file");
  if(EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)!=0)
    strcat(partition->info," Sparse superblock");
  if(EXT2_HAS_INCOMPAT_FEATURE(sb,EXT3_FEATURE_INCOMPAT_RECOVER)!=0)
    strcat(partition->info," Recover");
  if(EXT2_HAS_INCOMPAT_FEATURE(sb,EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)!=0)
    strcat(partition->info," Journal dev");
  if(le16(sb->s_block_group_nr)!=0)
  {
    strcat(partition->info," Backup superblock");
    if(verbose>0)
    {
      log_warning("\nblock_group_nr %u\n",le16(sb->s_block_group_nr));
    }
  }
  /* last mounted => date */
  return 0;
}

/*
Primary superblock is at 1024 (SUPERBLOCK_OFFSET)
Group 0 begin at s_first_data_block
*/
int recover_EXT2(disk_t *disk, const struct ext2_super_block *sb,partition_t *partition,const int verbose, const int dump_ind)
{
  if(test_EXT2(sb, partition)!=0)
    return 1;
  if(dump_ind!=0)
  {
    if(partition!=NULL && disk!=NULL)
      log_info("\nEXT2/EXT3 magic value at %u/%u/%u\n",
	  offset2cylinder(disk,partition->part_offset),
	  offset2head(disk,partition->part_offset),
	  offset2sector(disk,partition->part_offset));
    /* There is a little offset ... */
    dump_log(sb,DEFAULT_SECTOR_SIZE);
  }
  if(partition==NULL)
    return 0;
  set_EXT2_info(sb, partition, verbose);
  partition->part_type_i386=P_LINUX;
  partition->part_type_mac=PMAC_LINUX;
  partition->part_type_sun=PSUN_LINUX;
  partition->part_type_gpt=GPT_ENT_TYPE_LINUX_DATA;
  partition->part_size=(uint64_t)le32(sb->s_blocks_count)*(EXT2_MIN_BLOCK_SIZE<<le32(sb->s_log_block_size));
  guid_cpy(&partition->part_uuid, (const efi_guid_t *)&sb->s_uuid);
  if(verbose>0)
  {
    log_info("\n");
  }
  partition->sborg_offset=0x400;
  partition->sb_size=EXT2_SUPERBLOCK_SIZE;
  if(le16(sb->s_block_group_nr)>0)
  {
    unsigned long int block_nr=(le32(sb->s_first_data_block)+le16(sb->s_block_group_nr)*le32(sb->s_blocks_per_group));
    if(partition->part_offset< (uint64_t)block_nr * (EXT2_MIN_BLOCK_SIZE<<le32(sb->s_log_block_size)))
    {
      log_error("recover_EXT2: part_offset problem\n");
      return 1;
    }
    partition->sb_offset=(uint64_t)block_nr * (EXT2_MIN_BLOCK_SIZE<<le32(sb->s_log_block_size));
    partition->part_offset-=partition->sb_offset;
    log_warning("recover_EXT2: \"e2fsck -b %lu -B %u device\" may be needed\n",
        block_nr, partition->blocksize);
  }
  else
  {
    partition->sb_offset=0;
  }
  if(verbose>0)
  {
    log_info("recover_EXT2: s_block_group_nr=%u/%u, s_mnt_count=%u/%u, s_blocks_per_group=%u, s_inodes_per_group=%u\n",
        le16(sb->s_block_group_nr),
        (unsigned int)(le32(sb->s_blocks_count)/le32(sb->s_blocks_per_group)),
        le16(sb->s_mnt_count), le16(sb->s_max_mnt_count),
        (unsigned int)le32(sb->s_blocks_per_group),
        (unsigned int)le32(sb->s_inodes_per_group));
    log_info("recover_EXT2: s_blocksize=%u\n", partition->blocksize);
    log_info("recover_EXT2: s_blocks_count %u\n", (unsigned int)le32(sb->s_blocks_count));
    if(disk==NULL)
      log_info("recover_EXT2: part_size %lu\n", (long unsigned)(partition->part_size / DEFAULT_SECTOR_SIZE));
    else
      log_info("recover_EXT2: part_size %lu\n", (long unsigned)(partition->part_size / disk->sector_size));
  }
  return 0;
}

static int test_EXT2(const struct ext2_super_block *sb, partition_t *partition)
{
    /* There is a little offset ... */
  if(le16(sb->s_magic)!=EXT2_SUPER_MAGIC)
    return 1;
  if (le32(sb->s_free_blocks_count) > le32(sb->s_blocks_count)) return 2;
  if (le32(sb->s_free_inodes_count) > le32(sb->s_inodes_count)) return 3;
  if (le16(sb->s_errors)!=0 &&
      (le16(sb->s_errors) != EXT2_ERRORS_CONTINUE) &&
      (le16(sb->s_errors) != EXT2_ERRORS_RO) &&
      (le16(sb->s_errors) != EXT2_ERRORS_PANIC))
    return 4;
  if ((le16(sb->s_state) & ~(EXT2_VALID_FS | EXT2_ERROR_FS))!=0)
    return 5;
  if (le32(sb->s_blocks_count) == 0) /* reject empty filesystem */
    return 6;
  if(le32(sb->s_log_block_size)>2)  /* block size max = 4096, can be 8192 on alpha */
    return 7;
  if(partition==NULL)
    return 0;
  if(partition->part_size!=0 &&
      partition->part_size<(uint64_t)le32(sb->s_blocks_count)*(EXT2_MIN_BLOCK_SIZE<<le32(sb->s_log_block_size)))
    return 8;
  if(EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_HUGE_FILE)!=0 ||
      EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_GDT_CSUM)!=0 ||
      EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_DIR_NLINK)!=0 ||
      EXT2_HAS_RO_COMPAT_FEATURE(sb,EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE)!=0 ||
      EXT2_HAS_INCOMPAT_FEATURE(sb,EXT4_FEATURE_INCOMPAT_64BIT)!=0 ||
      EXT2_HAS_INCOMPAT_FEATURE(sb,EXT4_FEATURE_INCOMPAT_MMP)!=0)
    partition->upart_type=UP_EXT4;
  else if(EXT2_HAS_COMPAT_FEATURE(sb,EXT3_FEATURE_COMPAT_HAS_JOURNAL)!=0)
    partition->upart_type=UP_EXT3;
  else
    partition->upart_type=UP_EXT2;
  return 0;
}
