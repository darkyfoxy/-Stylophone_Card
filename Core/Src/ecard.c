/*
 * keyboad.c
 *
 *  Created on: 14 февр. 2023 г.
 *      Author: Vlad
 */
#include <stdio.h>
#include "stm32l4xx_hal.h"
#include "main.h"
#include "ecard.h"
#include <string.h>
#include "arm_math.h"

extern Universal_Buffer_TypeDef SPK_buffer_0;
extern Universal_Buffer_TypeDef SPK_buffer_1;

extern Universal_Buffer_TypeDef MIC_buffer_0;
extern Universal_Buffer_TypeDef MIC_buffer_1;

static int sampl_calculation(ecard_t *ecard,
                             Universal_Buffer_TypeDef *buffer,\
                             uint8_t note,\
                             uint16_t buff_size,\
                             uint16_t exp_delay)
{
  int32_t *p_buffer = (int32_t *)buffer->buffer;

  if(note == PAUSE)
  {
    memset(p_buffer, 0, buff_size*4);
  }
  else
  {
    const int16_t *note_form = ecard->table_note_form[note];
    uint8_t sampl_index = ecard->notes_table->index[note];
    uint16_t form_size = ecard->notes_table->size[note];

    if (ecard->vibrato == 1)
    {
      const float *temp_vib_form = ecard->vib_form;
      uint32_t sampl_vib_index = ecard->vibrato_index;
      uint32_t temp_vib_index = ecard->vibrato_temp_index;

      for (int i = 0; i < buff_size; i++)
	  {
    	float note_S = (float) (note_form[sampl_index]);
		float vib_S = temp_vib_form[sampl_vib_index];
		float mult = note_S * vib_S;

	    p_buffer[i] = (int16_t)mult;
	    sampl_index++;

	    if(sampl_index == form_size)
	    {
		  sampl_index = 0;
	    }

	    temp_vib_index++;

		if(temp_vib_index == 15)
		{
		  sampl_vib_index++;
		  temp_vib_index = 0;
		}

		if(sampl_vib_index == 480)
		{
		  sampl_vib_index = 0;
		  temp_vib_index = 0;
		}

	  }

      ecard->vibrato_temp_index = temp_vib_index;
      ecard->vibrato_index = sampl_vib_index;
    }
    else
    {
      for (int i = 0; i < buff_size; i++)
      {
        p_buffer[i] = note_form[sampl_index];
        sampl_index++;

        if(sampl_index == form_size)
        {
          sampl_index = 0;
        }
      }
    }
    ecard->notes_table->index[note] = sampl_index;
  }




  for (int ni = 0; ni < 20; ni++)
  {
    if (ecard->notes_table->exp_flag[ni] != 0)
    {
      const int16_t *note_form = ecard->table_note_form[ni];
      uint8_t sampl_index = ecard->notes_table->index[ni];
      uint8_t form_size = ecard->notes_table->size[ni];

      const float *temp_exp_form = ecard->exp_form;
      uint16_t sampl_exp_index = ecard->notes_table->exp_index[ni];
      uint16_t sampl_temp = ecard->notes_table->temp[ni];

      for (int i = 0; i < buff_size; i++)
      {
        float note_S = (float) (note_form[sampl_index]);
        float exp_S = temp_exp_form[sampl_exp_index];
        float mult = note_S * exp_S;
        p_buffer[i] = (int32_t)p_buffer[i] + (int32_t)(mult);

        sampl_index++;

        if(sampl_index == form_size)
        {
          sampl_index = 0;  
        }

        sampl_temp++;

        if(sampl_temp == exp_delay)
        {
          sampl_exp_index++;
          sampl_temp = 0;
        }

        if(sampl_exp_index == 500)
        {
          sampl_exp_index = 0;
          ecard->notes_table->exp_flag[ni] = 0;
          sampl_index = 0;
          break;
        }

      }

      ecard->notes_table->temp[ni] = sampl_temp;
      ecard->notes_table->exp_index[ni] = sampl_exp_index;
      ecard->notes_table->index[ni] = sampl_index;
    }
  }
  return 0;
}


static int init_note_form(ecard_t *ecard,\
                          note_table_t *notes_table,\
                          uint16_t *index,\
                          uint8_t *note_exp_flag,\
                          uint16_t *note_exp_index,\
                          const float *exp_form,\
						  const float *vib_form,\
                          uint16_t *temp)
{
  ecard->exp_form = exp_form;
  ecard->vib_form = vib_form;

  ecard->notes_table = notes_table;

  ecard->notes_table->index = index;

  ecard->notes_table->exp_flag = note_exp_flag;
  ecard->notes_table->exp_index = note_exp_index;
  ecard->notes_table->temp = temp;

  return 0;
}


static int read_keys(ecard_t *ecard)
{
  ecard->keys = 0;
  uint32_t port_c = ~(uint32_t)GPIOC->IDR;
  uint32_t port_b = ~(uint32_t)GPIOB->IDR;

  ecard->keys |= (port_c & (Am | Ashm)) >> 10;
  ecard->keys |= (port_b & Bm) >> 7;
  ecard->keys |= (port_c & (C | Csh | D | Dsh | E | F)) << 3;
  ecard->keys |= (port_b & (Fsh | G | Gsh)) << 9;
  ecard->keys |= (port_b & (A | Ash)) << 2;

  ecard->keys |= (port_c & Cp) << 8;
  ecard->keys |= (port_b & (Cshp | Dp)) << 4;
  ecard->keys |= (port_c & Dshp) << 12;

  ecard->keys |= (port_c & B) << 5;
  ecard->keys |= (port_c & Ep) << 11;

  return 0;
}


static int read_button(ecard_t *ecard)
{
  uint32_t port_c = ~(uint32_t)GPIOC->IDR;

  uint32_t temp = (port_c & (GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)) >> 12;

  if(temp != ecard->buttons)
  {
    ecard->but_temp++;
  }
  else
  {
    ecard->but_temp = 0;
  }

  if (ecard->but_temp >= 2)
  {
    ecard->buttons = temp;
    ecard->but_temp = 0;
  }
  return 0;
}

static int set_leds(ecard_t *ecard, uint8_t leds)
{
  GPIOA->ODR = leds;

  return 0;
}


static int set_note_table(ecard_t *ecard, const int16_t **table_note_form, uint16_t *size)
{
  ecard->table_note_form = table_note_form;

  ecard->notes_table->size = size;

  return 0;
}

static int prepare_to_DAC(ecard_t *ecard,\
	                      Universal_Buffer_TypeDef *in_buffer,\
	                      Universal_Buffer_TypeDef *adc_buffer,\
	                      uint16_t buff_size)
{
  int32_t *in_buff = (int32_t *)in_buffer->buffer;
  uint32_t *adc_buff = adc_buffer->buffer;

  if(SPK_buffer_0.valid == 1)
  {
    int16_t *spk_buff = (int16_t *)SPK_buffer_0.buffer;

    for (int i = 0; i < buff_size; i++)
    {
      adc_buff[i] = in_buff[i] + spk_buff[i] + 1500;
    }
    SPK_buffer_0.valid = 0;
  }
  else if(SPK_buffer_1.valid == 1)
  {
    int16_t *spk_buff = (int16_t *)SPK_buffer_1.buffer;

    for (int i = 0; i < buff_size; i++)
    {
      adc_buff[i] = in_buff[i] + spk_buff[i] + 1500;
    }
    SPK_buffer_1.valid = 0;
  }
  else
  {
    for (int i = 0; i < buff_size; i++)
    {
      adc_buff[i] = in_buff[i] + 1500;
    }
  }
  return 0;
}

static int prepare_to_MIC(ecard_t *ecard,\
	                      Universal_Buffer_TypeDef *in_buffer,\
	                      uint16_t buff_size)
{

  int32_t *in_buff = (int32_t *)in_buffer->buffer;

  if(MIC_buffer_0.valid == 1)
  {
    int16_t *spk_buff = (int16_t *)MIC_buffer_0.buffer;

    for (int i = 0; i < buff_size; i++)
    {
      spk_buff[i] = in_buff[i];
    }
    MIC_buffer_0.valid = 0;
  }
  else if(MIC_buffer_1.valid == 1)
  {
    int16_t *spk_buff = (int16_t *)MIC_buffer_1.buffer;

    for (int i = 0; i < buff_size; i++)
    {
      spk_buff[i] = in_buff[i];
    }
    MIC_buffer_1.valid = 0;
  }
  return 0;
}

int ecard_init(ecard_t *ecard)
{
  ecard->buttons = 0x00;
  ecard->keys = 0x00;

  ecard->vibrato = 0x00;
  ecard->vibrato_index = 0x00;
  ecard->vibrato_temp_index = 0x00;

  ecard->read_keys               = read_keys;
  ecard->init_note_form          = init_note_form;
  ecard->set_note_table       = set_note_table;

  ecard->sampl_calculation       = sampl_calculation;
  ecard->prepare_to_DAC          = prepare_to_DAC;
  ecard->prepare_to_MIC          = prepare_to_MIC;

  ecard->read_button             = read_button;

  ecard->set_leds                = set_leds;
  return 0;
}
