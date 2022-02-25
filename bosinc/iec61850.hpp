#ifndef IEC61850_HPP
#define IEC61850_HPP

#include <array>
#include <tuple>
#include <sstream>
#include <fcntl.h>
#include <errno.h>
#include <algorithm>
#include "BatteryInterface.hpp"
#include "hal_thread.h"
#include "iec61850_client.h"

class IEC61850 : public PhysicalBattery {
    public:
        ~IEC61850();
        std::string get_type_string() override;
        IEC61850(const std::string &name, std::chrono::milliseconds staleness, std::string LogicalDevice_Name, std::string ZBAT_Name, std::string ZBTC_Name, std::string ZINV_Name);
        IEC61850(const std::string &name, std::chrono::milliseconds staleness, std::string LogicalDevice_Name, std::string ZBAT_Name, std::string ZBTC_Name, std::string ZINV_Name, std::string hostname, int tcpPort);
        uint32_t set_current(int64_t target_current_mA, bool is_greater_than_target, void* other_data) override;
        
    protected:
        BatteryStatus refresh() override;
        // uint32_t set_current(int64_t target_current_mA, bool is_greater_than_target, void* other_data) override;

    private:
        IedConnection con;
        IedClientError error;
        std::string LogicalDevice_Name;
        std::string ZBAT_Name, ZBTC_Name, ZINV_Name;
        bool check_MmsValue(MmsValue* value);
        bool create_iec61850_client(std::string hostname, int tcpPort);
};

#endif