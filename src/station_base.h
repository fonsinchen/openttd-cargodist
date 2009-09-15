/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_base.h Base classes/functions for stations. */

#ifndef STATION_BASE_H
#define STATION_BASE_H

#include "base_station_base.h"
#include "airport.h"
#include "cargopacket.h"
#include "cargo_type.h"
#include "vehicle_type.h"
#include "industry_type.h"
#include "core/geometry_type.hpp"
#include "linkgraph/linkgraph_types.h"
#include <list>
#include <map>
#include <set>

typedef Pool<BaseStation, StationID, 32, 64000> StationPool;
extern StationPool _station_pool;

static const byte INITIAL_STATION_RATING = 175;

class LinkStat {
public:
	uint capacity;
	uint frozen;
	uint usage;
	LinkStat() : capacity(0), frozen(0), usage(0) {}

	inline LinkStat & operator*=(uint factor) {
		capacity *= factor;
		usage *= factor;
		return *this;
	}

	inline LinkStat & operator/=(uint divident) {
		capacity /= divident;
		if (capacity < frozen) {
			capacity = frozen;
		}
		usage /= divident;
		return *this;
	}

	inline LinkStat & operator+=(const LinkStat & other)
	{
		this->capacity += other.capacity;
		this->usage += other.usage;
		this->frozen += other.frozen;
		return *this;
	}

	inline void Clear()
	{
		this->capacity = 0;
		this->usage = 0;
		this->frozen = 0;
	}
};

class FlowStat {
public:
	FlowStat(StationID st = INVALID_STATION, uint p = 0, uint s = 0) :
		planned(p), sent(s), via(st) {}
	uint planned;
	uint sent;
	StationID via;
	struct comp {
		bool operator()(const FlowStat & x, const FlowStat & y) const {
			int diff_x = (int)x.planned - (int)x.sent;
			int diff_y = (int)y.planned - (int)y.sent;
			if (diff_x != diff_y) {
				return diff_x > diff_y;
			} else {
				return x.via > y.via;
			}
		}
	};

	inline FlowStat & operator*=(uint factor) {
		planned *= factor;
		sent *= factor;
		return *this;
	}

	inline FlowStat & operator/=(uint divident) {
		planned /= divident;
		sent /= divident;
		return *this;
	}

	inline FlowStat & operator+=(const FlowStat & other)
	{
		assert(this->via == INVALID_STATION || other.via == INVALID_STATION || this->via == other.via);
		this->via = other.via;
		this->planned += other.planned;
		this->sent += other.sent;
		return *this;
	}

	inline void Clear()
	{
		this->planned = 0;
		this->sent = 0;
		this->via = INVALID_STATION;
	}
};

typedef std::set<FlowStat, FlowStat::comp> FlowStatSet; ///< percentage of flow to be sent via specified station (or consumed locally)
typedef std::map<StationID, LinkStat> LinkStatMap;
typedef std::map<StationID, FlowStatSet> FlowStatMap; ///< flow descriptions by origin stations

struct GoodsEntry {
	enum AcceptancePickup {
		ACCEPTANCE,
		PICKUP
	};

	GoodsEntry() :
		acceptance_pickup(0),
		days_since_pickup(255),
		rating(INITIAL_STATION_RATING),
		last_speed(0),
		last_age(255),
		last_component(0)
	{}

	byte acceptance_pickup;
	byte days_since_pickup;
	byte rating;
	byte last_speed;
	byte last_age;
	StationCargoList cargo; ///< The cargo packets of cargo waiting in this station
	uint supply;
	FlowStatMap flows;      ///< The planned flows through this station
	LinkStatMap link_stats; ///< capacities and usage statistics for outgoing links
	LinkGraphComponentID last_component; ///< the component this station was last part of in this cargo's link graph
	
	FlowStat GetSumFlowVia(StationID via) const;

	/**
	 * update the flow stats for count cargo from source sent to next
	 */
	void UpdateFlowStats(StationID source, uint count, StationID next);

	/**
	 * update the flow stats for count cargo that cannot be delivered here
	 * return the direction where it is sent
	 */
	StationID UpdateFlowStatsTransfer(StationID source, uint count, StationID curr);
private:
	void UpdateFlowStats(FlowStatSet & flow_stats, FlowStatSet::iterator flow_it, uint count);
};


typedef SmallVector<Industry *, 2> IndustryVector;

/** Station data structure */
struct Station : SpecializedStation<Station, false> {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == ROADSTOP_BUS ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const struct RoadVehicle *v) const;

	const AirportFTAClass *Airport() const
	{
		if (airport_tile == INVALID_TILE) return GetAirport(AT_DUMMY);
		return GetAirport(airport_type);
	}

	RoadStop *bus_stops;    ///< All the road stops
	RoadStop *truck_stops;  ///< All the truck stops
	TileIndex airport_tile; ///< The location of the airport
	TileIndex dock_tile;    ///< The location of the dock

	IndustryType indtype;   ///< Industry type to get the name from

	StationHadVehicleOfTypeByte had_vehicle_of_type;

	byte time_since_load;
	byte time_since_unload;
	byte airport_type;

	uint64 airport_flags;   ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32

	byte last_vehicle_type;
	std::list<Vehicle *> loading_vehicles;
	GoodsEntry goods[NUM_CARGO];  ///< Goods at this station
	uint32 town_acc; ///< Bitmask of cargos accepted by town houses and headquarters

	IndustryVector industries_near; ///< Cached list of industries near the station that can accept cargo, @see DeliverGoodsToIndustry()

	Station(TileIndex tile = INVALID_TILE);
	~Station();

	void AddFacility(StationFacility new_facility_bit, TileIndex facil_xy);

	/**
	 * Marks the tiles of the station as dirty.
	 *
	 * @ingroup dirty
	 */
	void MarkTilesDirty(bool cargo_change) const;

	void UpdateVirtCoord();

	/* virtual */ uint GetPlatformLength(TileIndex tile, DiagDirection dir) const;
	/* virtual */ uint GetPlatformLength(TileIndex tile) const;
	void RecomputeIndustriesNear();
	static void RecomputeIndustriesNearForAll();

	uint GetCatchmentRadius() const;
	Rect GetCatchmentRect() const;

	/* virtual */ FORCEINLINE bool TileBelongsToRailStation(TileIndex tile) const
	{
		return IsRailStationTile(tile) && GetStationIndex(tile) == this->index;
	}

	/* virtual */ uint32 GetNewGRFVariable(const ResolverObject *object, byte variable, byte parameter, bool *available) const;

	/* virtual */ void GetTileArea(TileArea *ta, StationType type) const;
};

#define FOR_ALL_STATIONS(var) FOR_ALL_BASE_STATIONS_OF_TYPE(Station, var)

#endif /* STATION_BASE_H */
