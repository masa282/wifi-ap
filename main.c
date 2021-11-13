#include<sys/types.h>
#include<sys/ioctl.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<netinet/in.h>
#include<net/if.h>
#include<linux/if_packet.h>
#include<linux/if_ether.h>
#include "ieee80211_radiotap.h"
#include "ieee80211.h"


#define IEEE80211_ADDR_LEN    6
#define IEEE80211_RATE_BASIC  0x01
#define IEEE80211_RATE_VAL    0x0e
#define BEACON_INTERVAL       102400


struct ieee80211_beacon{
  uint64_t beacon_timestamp;
  uint16_t beacon_interval;
  uint16_t beacon_capabilities;
}__attribute__((__packed__));


struct ieee80211_info_element{
  uint8_t info_elemid;
  uint8_t info_length;
  uint8_t *info[0];
}__attribute__((__packed__));


struct AccessPointDescriptor
{
  uint8_t macAddress[IEEE80211_ADDR_LEN];
  const uint8_t *ssid;
  size_t ssidLength;
  const uint8_t *dataRates;
  size_t dataRatesLength;
};


static const uint8_t IEEE80211_BROADCAST_ADDR[IEEE80211_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t IEEE80211B_DEFAULT_RATES[] = {
  IEEE80211_RATE_BASIC | 2,
  IEEE80211_RATE_BASIC | 4,
  11,
  22
};

#define IEEE80211B_DEFAULT_RATES_LENGTH sizeof(IEEE80211B_DEFAULT_RATES)


static struct AccessPointDescriptor ap = {
  {0x08, 0x00, 0x27, 0xa1, 0x58, 0x8d},
  (const uint8_t *)"Hello World!", 12,
  IEEE80211B_DEFAULT_RATES, IEEE80211B_DEFAULT_RATES_LENGTH,
};

int opensocket(const char device[IFNAMSIZ])
{
  struct ifreq ifr;
  struct sockaddr_ll sll;
  const int protocol = ETH_P_ALL;
  int soc = -1;

  soc = socket(PF_PACKET, SOCK_RAW, htons(protocol));
  if(soc<0){
    perror("[-]socket failed");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
  if(ioctl(soc, SIOCGIFINDEX, &ifr)<0){
    perror("[-]ioctl[SIOCGIFINDEX]");
    close(soc);
    return -1;
  }

  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(protocol);
  sll.sll_ifindex = ifr.ifr_ifindex;
  if(bind(soc, (struct sockaddr *)&sll, sizeof(sll))<0){
    perror("[-]bind[AF_PACKET]");
    close(soc);
    return -1;
  }

  return soc;
}

uint8_t *constructBeaconPacket(uint8_t dataRate, uint8_t channel, size_t beaconLength)
{
  uint8_t dataRateValue = (dataRate & IEEE80211_RATE_VAL);

  uint8_t *packet = (uint8_t*)malloc(beaconLength);
  if(packet == NULL){
    return NULL;
  }

  struct ieee80211_radiotap_header *radiotap = (struct ieee80211_radiotap_header *)packet;
  uint8_t *packetIterator = packet + sizeof(*radiotap);

  radiotap->it_version = 0;
  radiotap->it_len = sizeof(*radiotap) + sizeof(dataRate);
  radiotap->it_present = (1 << IEEE80211_RADIOTAP_RATE);

  *packetIterator = (dataRate & IEEE80211_RATE_VAL);
  packetIterator ++;

  struct ieee80211_frame *mac_header = (struct ieee80211_frame*)packetIterator;
  packetIterator += sizeof(*mac_header);

  mac_header->i_fc = 0;
  mac_header->i_fc = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON;
  mac_header->i_dur = 0;

  memcpy(mac_header->i_addr1, IEEE80211_BROADCAST_ADDR, IEEE80211_ADDR_LEN);
  memcpy(mac_header->i_addr2, ap.macAddress, IEEE80211_ADDR_LEN);
  memcpy(mac_header->i_addr3, ap.macAddress, IEEE80211_ADDR_LEN);

  struct ieee80211_beacon *beacon = (struct ieee80211_beacon *)packetIterator;
  packetIterator += sizeof(*beacon);

  beacon->beacon_timestamp = 0;
  beacon->beacon_interval = htole16(BEACON_INTERVAL/1024);
  beacon->beacon_capabilities = htole16(0x0001);

  struct ieee80211_info_element *info = (struct ieee80211_info_element *)packetIterator;
  packetIterator += sizeof(struct ieee80211_info_element) + ap.ssidLength;

  info->info_elemid = IEEE80211_ELEMID_SSID;
  info->info_length = ap.ssidLength;
  memcpy(info->info, ap.ssid, ap.ssidLength);

  info = (struct ieee80211_info_element *)packetIterator;
  packetIterator += sizeof(struct ieee80211_info_element) + ap.dataRatesLength;

  info->info_elemid = IEEE80211_ELEMID_RATES;
  info->info_length = ap.dataRatesLength;
  memcpy(info->info, ap.dataRates, ap.dataRatesLength);

  info = (struct ieee80211_info_element *)packetIterator;
  packetIterator += sizeof(struct ieee80211_info_element) + sizeof(channel);

  info->info_elemid = IEEE80211_ELEMID_DSPARAMS;
  info->info_length = sizeof(channel);
  memcpy(info->info, &channel, sizeof(channel));

  return packet;
}


int main(int argc, char *argv[])
{
  if(argc != 3){
    printf("[-]Fake AP [deveice] [channel]\n");
    return -1;
  }

  uint64_t channel = strtol(argv[2], NULL, 10);
  if(channel <= 0 || 255 <= channel){
    printf("[-]The channel must be between 1 and 255.\n");
    return -1;
  }
  
  const char *device = argv[1];
  int soc = opensocket(device);
  if(soc<0){
    fprintf(stderr, "[-]Error opening socket: %s\n", device);
    return -1;
  }
  printf("[+]Socket was created\n");
 
  const uint8_t dataRate = 0x4;
  size_t beaconLength = sizeof(struct ieee80211_radiotap_header) + sizeof(dataRate) + sizeof(struct ieee80211_frame) + sizeof(struct ieee80211_beacon) + sizeof(struct ieee80211_info_element)*3 + ap.ssidLength + ap.dataRatesLength + sizeof(channel);
  uint8_t *beaconPacket = (uint8_t *)malloc(sizeof(beaconLength));
  printf("[+]Beacon Length: %d\n", beaconLength);

  beaconPacket = constructBeaconPacket(dataRate, channel, beaconLength);
  printf("[+]Created Beacon Frame: %d\n");
  
  while(1)
  {
    ssize_t bytes = write(soc, beaconPacket, beaconLength);
    if(bytes < (ssize_t)beaconLength){
      perror("[-]Error sending packet");
      break;
    }
    printf("[+]Beacon was sent\n");
  }
  
  close(soc);
  free(beaconPacket);
}