#ifndef FAT32_SD_SPI
#define FAT32_SD_SPI
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/spi.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>


typedef struct fat32_data{
        uint32_t partition_lba;
        
        uint16_t bytes_per_sector;
        uint8_t sectors_per_cluster;
        uint16_t reserved_sectors;
        uint8_t number_of_fats;
        uint32_t FAT_size;
        uint32_t root_cluster;
        uint32_t first_data_sector;

        uint32_t root_sector;
}fat32_data;


typedef struct directory_entry{
        uint8_t name[11];
        uint8_t attr;
        uint8_t ntres;
        uint8_t crtimetenth;
        uint8_t crttime[2];
        uint8_t crtdate[2];
        uint8_t lstaccdate[2];
        uint8_t fsclushi[2];
        uint8_t wrttime[2];
        uint8_t wrtdate[2];
        uint8_t fscluslo[2];
        uint8_t filesize[4];
}__attribute__((packed)) directory_entry;

static bool block_addressing = false;
void extract_fat32_info(fat32_data *f32,uint8_t *block);

int sd_card_init(uint32_t spi,uint32_t cs_port,uint32_t cs_pin);

int sd_hw_init(uint32_t spi,uint32_t cs_port, uint32_t cs_pin){
  rcc_periph_clock_enable(RCC_SPI1);
  rcc_periph_clock_enable(RCC_GPIOA);
  rcc_periph_clock_enable(RCC_AFIO);

  gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
                GPIO5 | GPIO7);
  gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO6);
  gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL,
                cs_pin);

  rcc_periph_reset_pulse(RST_SPI1);

  spi_init_master(
      spi,
      SPI_CR1_BAUDRATE_FPCLK_DIV_256,
      SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
      SPI_CR1_CPHA_CLK_TRANSITION_1,
      SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST
      );

  spi_enable_software_slave_management(spi);
  spi_set_nss_high(spi);

  spi_enable(spi);
  return sd_card_init(spi, cs_port, cs_pin);
}

int sd_card_init(uint32_t spi,uint32_t cs_port,uint32_t cs_pin){
  int res = 0;

  gpio_set(cs_port, cs_pin);

  for (int i = 0; i < 20; i++) {
    spi_xfer(spi, 0xFF);
  }

  gpio_clear(cs_port, cs_pin);

  // send cmd0 for power up
  uint8_t CMD0[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
  for (int i = 0; i < 6; i++) {
    spi_xfer(spi, CMD0[i]);
  }

  for (int i = 0; i < 255; i++) {
    res = spi_xfer(spi, 0xFF);
    if (res != 0xFF)
      break;
  }

  gpio_set(cs_port, cs_pin);
  spi_xfer(spi, 0xFF);
  
  if(res != 0x01){
          return false;
  }
  
  // add a delay before reading the next byte so its not caught in an unready
  // state

  gpio_clear(cs_port, cs_pin);

  // send the cmd8 for setup of spi and read the response should be 0x01 0x00
  // 0x00 0x01 0xAA but the crc is fine ig????
  uint8_t CMD8[6] = {0x48, 0x00, 0x00, 0x01, 0xAA, 0x87};
  uint16_t CMD8_res[10] = {0};
  int cursor = 0;
  if (res == 0x01) {
    for (int i = 0; i < 6; i++) {
      spi_xfer(spi, CMD8[i]);
    }
    int tmp = 0xff;
    do {
      tmp = spi_xfer(spi, 0xFF);
    } while (tmp == 0xFF);
    CMD8_res[0] = tmp;
    for (int i = 0; i < 5; i++) {
      CMD8_res[i + 1] = spi_xfer(spi, 0xFF);
    }
  }

  

  int iterations = 1000;
  do {

    gpio_set(cs_port, cs_pin);
    spi_xfer(spi, 0xFF);
    gpio_clear(cs_port, cs_pin);
    // send cmd55  response should be 0x01
    uint8_t CMD55[6] = {0x77, 0x00, 0x00, 0x00, 0x00, 0X65};

    for (int i = 0; i < 6; i++) {
      spi_xfer(spi, CMD55[i]);
    }

    for (int i = 0; i < 8; i++) {
      res = spi_xfer(spi, 0xFF);
      if (res != 0xFF)
        break;
    }

    // send cmd41 and wait for 0x01
    gpio_set(cs_port, cs_pin);
    spi_xfer(spi, 0xFF);
    gpio_clear(cs_port, cs_pin);

    uint8_t CMD41[6] = {0x69, 0x40, 0x00, 0x00, 0x00, 0x77};
    for (int i = 0; i < 6; i++) {
      spi_xfer(spi, CMD41[i]);
    }
    for (int i = 0; i < 8; i++) {
      res = spi_xfer(spi, 0xFF);
      if (res != 0xFF)
        break;
    }
    iterations--;
    if(iterations == 0)return false;
  } while (res == 0x01 || res == 0xff);
  
  gpio_set(cs_port, cs_pin);
  spi_xfer(spi, 0xFF);
  gpio_clear(cs_port, cs_pin);

  uint8_t CMD58[6] = {0x7A, 0x00, 0x00, 0x00, 0x00, 0xFF};
  for (int i = 0; i < 6; i++) {
    res = spi_xfer(spi, CMD58[i]);
  }

  uint8_t res1;
  for (int i = 0; i < 8; i++) {
    res1 = spi_xfer(spi, 0xFF);
    if (res1 != 0xFF)
      break;
  }

  int b;

  uint8_t CMD58_res[5];
  CMD58_res[0] = res1;
  for (int i = 1; i <= 4; i++) {
    res = spi_xfer(spi, 0xFF);
    CMD58_res[i] = res;
  }

  if (CMD58_res[1] & 0x40) {
    block_addressing = true;
  }
  return true;

}

bool sd_card_read_block(uint32_t SPI, uint32_t cs_port, uint32_t cs_pin, uint32_t block, uint8_t *data) {
  uint8_t cmd[6] = {0x51, 0x00, 0x00, 0x00, 0x00, 0xFF};
  if (!block_addressing)
    block *= 512;
  block = __builtin_bswap32(block);
  *((uint32_t *)(cmd + 1)) = block;

  // gpio_set(cs_port,cs_pin);
  // spi_xfer(SPI,0xFF);
  gpio_clear(cs_port, cs_pin);

  uint8_t res = 0;
  for (int i = 0; i < 6; i++) {
    res = spi_xfer(SPI, cmd[i]);
  }

  uint8_t r1 = 0xff;

  for (int i = 0; i < 8; i++) {
    res = spi_xfer(SPI, 0xFF);
    if (res != 0xFF)
      break;
  }
  
  r1 = res;
  if (r1 != 0x00) {
        gpio_set(cs_port, cs_pin);
        spi_xfer(SPI, 0xFF);
        return false;
  }

  for (int i = 0; i < 2048; i++) {
    res = spi_xfer(SPI, 0xFF);
    if (res == 0xFE)
      break;
  }
  if (res != 0xFE) {
        gpio_set(cs_port, cs_pin);
        spi_xfer(SPI, 0xFF);
        return false;
  }

  for (int i = 0; i < 512; i++) {
    data[i] = spi_xfer(SPI, 0xFF);
  }

  gpio_set(cs_port, cs_pin);
  spi_xfer(SPI, 0xFF);

  return true;
}

bool fat32_init(uint32_t spi, uint32_t cs_port, uint32_t cs_pin, fat32_data *f32){
        uint8_t data_buffer[512];
        memset(data_buffer,0,512);

        bool read_res = sd_card_read_block(spi,cs_port,cs_pin,0,data_buffer);
        f32->partition_lba = *((uint32_t *)(data_buffer + 0x1c6));

        sd_card_read_block(spi,cs_port,cs_pin, f32->partition_lba, data_buffer);
        extract_fat32_info(f32,data_buffer);
}

directory_entry* f32_get_directory_entries(uint32_t spi, uint32_t cs_port, uint32_t cs_pin, fat32_data *f32){
        directory_entry* entries = (directory_entry*)malloc(sizeof(directory_entry)*16);
        sd_card_read_block(spi,cs_port,cs_pin,f32->root_sector, (uint8_t*)entries);
        return entries;
}

void extract_fat32_info(fat32_data *f32,uint8_t *block){
        f32->bytes_per_sector = *((uint16_t *)(block + 0x0B));
        f32->sectors_per_cluster = *((uint8_t *)(block + 0x0D));
        f32->reserved_sectors = *((uint16_t *)(block + 0x0E));
        f32->number_of_fats = *((uint8_t *)(block + 0x10));
        f32->FAT_size = *((uint32_t *)(block + 0x24));
        f32->root_cluster = *((uint32_t *)(block + 0x2C));
        f32->first_data_sector = f32->partition_lba + f32->reserved_sectors + f32->number_of_fats * f32->FAT_size; // this is in blocks not bytes

        f32->root_sector = f32->first_data_sector + (f32->root_cluster - 2) * f32->sectors_per_cluster;
}

#endif
