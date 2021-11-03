#ifndef SPLITTER_POLICY_HPP
#define SPLITTER_POLICY_HPP

#include "BatteryInterface.hpp"
#include "BOSDirectory.hpp"

class SplitterPolicy : public VirtualBattery {
protected: 
    std::string src_name;
    BOSDirectory *pdirectory;
    Battery *source;
public: 
    SplitterPolicy(
        const std::string &policy_name, 
        const std::string &src_name, 
        BOSDirectory &directory
    ) : VirtualBattery(policy_name), src_name(src_name), pdirectory(&directory) 
    {
        this->type = BatteryType::SPLIT_POLICY;
        source = directory.get_battery(src_name);
        if (!source) {
            warning("source not found!");
        }
    }
    Battery *get_source() {
        return this->source;
    }
    BatteryStatus refresh() override {
        return this->status;
    }
    uint32_t set_current(int64_t current_mA, bool is_greater_than) override {
        return 0;
    }

};


class ProportionalPolicy : public SplitterPolicy {
public:
    struct Scale {
        double state_of_charge;
        double max_capacity;
        double max_discharge_rate;
        double max_charge_rate;
        static bool within_01_range(double num) {
            return 0.0 <= num && num <= 1.0;
        }
        Scale(double soc, double max_cap, double max_discharge_rate, double max_charge_rate) {
            if (!(within_01_range(soc) && within_01_range(max_cap) && within_01_range(max_discharge_rate) && within_01_range(max_charge_rate))) {
                warning("Scale parameter not within range [0.0, 1.0]");
                this->state_of_charge = this->max_capacity = this->max_discharge_rate = this->max_charge_rate = 0.0;
            } else {
                this->state_of_charge = soc;
                this->max_capacity = max_cap;
                this->max_discharge_rate = max_discharge_rate;
                this->max_charge_rate = max_charge_rate;
            }
        }
        Scale(double proportion=0.0) {
            if (!within_01_range(proportion)) {
                warning("Scale parameter not within range [0.0, 1.0]");
                this->state_of_charge = this->max_capacity = this->max_discharge_rate = this->max_charge_rate = 0.0;
            } else {
                this->state_of_charge = this->max_capacity = this->max_discharge_rate = this->max_charge_rate = proportion;
            }
        }
        Scale operator-(const Scale &other) {
            if (this->state_of_charge >= other.state_of_charge && 
                this->max_capacity >= other.max_capacity && 
                this->max_discharge_rate >= other.max_discharge_rate && 
                this->max_charge_rate >= other.max_charge_rate) 
            {
                return Scale(
                    this->state_of_charge - other.state_of_charge, 
                    this->max_capacity - other.max_capacity, 
                    this->max_discharge_rate - other.max_discharge_rate, 
                    this->max_charge_rate - other.max_charge_rate);
            } else {
                warning("not enough resource to subtract!");
                return Scale(0.0);
            }
        }

        Scale operator+(const Scale &other) {
            if (within_01_range(this->state_of_charge + other.state_of_charge) && 
                within_01_range(this->max_capacity + other.max_capacity) && 
                within_01_range(this->max_discharge_rate + other.max_discharge_rate) && 
                within_01_range(this->max_charge_rate >= other.max_charge_rate)) 
            {
                return Scale(
                    this->state_of_charge + other.state_of_charge, 
                    this->max_capacity + other.max_capacity, 
                    this->max_discharge_rate + other.max_discharge_rate, 
                    this->max_charge_rate + other.max_charge_rate);
            } else {
                warning("sum not within [0, 1] range!");
                return Scale(0.0);
            }
        }
    };


protected:
    std::map<Battery*, int64_t> current_map;
    std::map<Battery*, Scale> scale_map;
    std::list<Battery*> children;
public: 
    ProportionalPolicy(const std::string &policy_name, const std::string &src_name, BOSDirectory &directory, Battery *first_battery) : 
        SplitterPolicy(policy_name, src_name, directory)
    {
        // note: the first battery should be created and inserted already 
        this->current_map.insert(std::make_pair(first_battery, 0));
        this->scale_map.insert(std::make_pair(first_battery, Scale(1.0)));
        this->children.push_back(first_battery);
    }

    std::list<Battery*> get_children() {
        lockguard_t lkg(this->lock);
        return this->children;
    }

    BatteryStatus get_status_of(Battery *child) {
        lockguard_t lkg(this->lock);
        Battery *source = this->source;
        BatteryStatus source_status = source->get_status();
        Scale &scale = this->scale_map[child];
        int64_t estimated_soc = child->get_estimated_soc();
        int64_t total_estimated_soc = 0;
        for (Battery *c : children) {
            total_estimated_soc += c->get_estimated_soc();
        }
        int64_t total_actual_soc = source_status.state_of_charge_mAh;
        int64_t actual_soc = (int64_t)((double)estimated_soc / (double)total_estimated_soc * (double)total_actual_soc);
        
        BatteryStatus status;
        status.voltage_mV = source_status.voltage_mV;
        status.current_mA = current_map[child];
        status.state_of_charge_mAh = actual_soc;
        status.max_capacity_mAh = source_status.max_capacity_mAh * scale.max_capacity;
        status.max_charging_current_mA = source_status.max_charging_current_mA * scale.max_charge_rate;
        status.max_discharging_current_mA = source_status.max_discharging_current_mA * scale.max_discharge_rate;
        return status;
    }

    uint32_t schedule_set_current_of(Battery *child, int64_t target_current_mA, bool is_greater_than_target, timepoint_t when_to_set, timepoint_t until_when) {
        lockguard_t lkg(this->lock);
        Scale &scale = this->scale_map[child];
        Battery *source = this->source;
        BatteryStatus source_status = source->get_status();

        if (target_current_mA > source_status.max_discharging_current_mA * scale.max_discharge_rate || 
            -target_current_mA > source_status.max_charging_current_mA * scale.max_charge_rate) {
            warning("target current too high, event not scheduled");
            return 0;
        }
        this->current_map[child] = target_current_mA;
        int64_t new_currents = 0;
        for (auto &p : this->current_map) {
            new_currents += p.second;
        }
        source->schedule_set_current(new_currents, is_greater_than_target, when_to_set, until_when);
        return 0;
    }

    BatteryStatus fork_from(const std::string &from_name, const std::string &child_name, const BatteryStatus &target_status) {
        lockguard_t lkg(this->lock);
        if (!pdirectory->name_exists(from_name)) {
            warning("Battery ", from_name, " does not exist");
            return target_status;
        }
        if (pdirectory->name_exists(child_name)) {
            warning("Battery ", child_name, "exists");
            return target_status;
        }

        Battery *from_battery = pdirectory->get_battery(from_name);
        BatteryStatus from_status = from_battery->get_status();

        if (from_status.current_mA > 0) {
            warning("from battery is in use");
        }
        
        BatteryStatus actual_status;
        actual_status.voltage_mV = from_status.voltage_mV;
        actual_status.current_mA = 0;
        actual_status.state_of_charge_mAh = std::min(target_status.state_of_charge_mAh, from_status.state_of_charge_mAh);
        actual_status.max_capacity_mAh = std::min(target_status.max_capacity_mAh, from_status.max_capacity_mAh);
        actual_status.max_charging_current_mA = std::min(target_status.max_charging_current_mA, from_status.max_charging_current_mA);
        actual_status.max_discharging_current_mA = std::min(target_status.max_discharging_current_mA, from_status.max_discharging_current_mA);

        BatteryStatus source_status = this->source->get_status();

        // now compute the new scale 
        Scale scale(
            (double)actual_status.state_of_charge_mAh / source_status.state_of_charge_mAh,
            (double)actual_status.max_capacity_mAh / source_status.max_capacity_mAh,
            (double)actual_status.max_discharging_current_mA / source_status.max_discharging_current_mA,
            (double)actual_status.max_charging_current_mA / source_status.max_charging_current_mA
        );

        Battery *child_battery = pdirectory->get_battery(child_name);
        this->scale_map[child_battery] = scale;
        this->current_map[child_battery] = 0;
        this->scale_map[from_battery] = this->scale_map[from_battery] - scale;
        from_battery->reset_estimated_soc();
        return actual_status;
    }


    // void merge_to(const std::string &name, const std::string &to_name) {
    //     lockguard_t lkg(this->lock);
    //     if (!pdirectory->name_exists(to_name)) {
    //         warning("Battery ", from_name, " does not exist");
    //         return;
    //     }
    //     if (!pdirectory->name_exists(name)) {
    //         warning("Battery ", child_name, "does not exist");
    //         return;
    //     }
    //     Battery *rbat = pdirectory->get_battery(name);
    //     Battery *to_bat = pdirectory->get_battery(to_name);
    //     Scale rm_scale = this->scale_map[rbat];
    //     int64_t rm_soc = rbat->get_estimated_soc();
    //     int64_t to_soc = to_bat->get_estimated_soc();

    //     to_soc += rm_soc;

    //     to_bat->set_estimated_soc(to_soc);
    // }
    



};

#endif // ! SPLITTER_POLICY_HPP
