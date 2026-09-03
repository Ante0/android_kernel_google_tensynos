/*
  *
  **************************************************************************
  **                        STMicroelectronics                            **
  **************************************************************************
  *                                                                        *
  *         FTS Hardware definition                                       **
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file fts_hardware.h
  * \brief Contains all the definitions and structs related to the fingertip
  * chip
  */

#ifndef FTS_HARDWARE_H
#define FTS_HARDWARE_H

// #define SPRUCE
#define ANGSANA

#ifndef SPRUCE
#define CHIP_ID				0x4654 /* /< chip id of finger tip
						   * device, angsana */
#else
#define CHIP_ID				0x3652 /* /< chip id of finger tip
					       * device, spruce */
#endif

#define CHIP_REV_2_0   0x08

#endif