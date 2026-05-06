#ifndef __BOBBYRMAILORDER_H
#define __BOBBYRMAILORDER_H

#include "LaptopSave.h"

#include <vector>



void GameInitBobbyRMailOrder(void);
void EnterBobbyRMailOrder(void);
void ExitBobbyRMailOrder(void);
void HandleBobbyRMailOrder(void);
void RenderBobbyRMailOrder(void);

void BobbyRayMailOrderEndGameShutDown(void);
void EnterInitBobbyRayOrder(void);
void AddJohnsGunShipment(void);

void CreateBobbyRayOrderTitle(void);
void DestroyBobbyROrderTitle(void);
void DrawBobbyROrderTitle(void);

void DisplayPurchasedItems( BOOLEAN fCalledFromOrderPage, UINT16 usGridX, UINT16 usGridY, BobbyRayPurchaseStruct *pBobbyRayPurchase, BOOLEAN fJustDisplayTitles, INT32 iOrderNum );


struct NewBobbyRayOrderStruct
{
	BOOLEAN fActive;
	UINT8   ubDeliveryLoc;				// the city the shipment is going to
	UINT8   ubDeliveryMethod;			// type of delivery: next day, 2 days ...
	BobbyRayPurchaseStruct BobbyRayPurchase[ MAX_PURCHASE_AMOUNT ];
	UINT8   ubNumberPurchases;

	UINT32  uiPackageWeight;
	UINT32  uiOrderedOnDayNum;

	BOOLEAN fDisplayedInShipmentPage;

	UINT8   ubFiller[7]; // XXX HACK000B
};


extern std::vector<NewBobbyRayOrderStruct> gpNewBobbyrShipments;

UINT16 CountNumberOfBobbyPurchasesThatAreInTransit(void);

void NewWayOfLoadingBobbyRMailOrdersToSaveGameFile(HWFILE);
void NewWayOfSavingBobbyRMailOrdersToSaveGameFile(HWFILE);


// ----- Console-verb bridges ---------------------------------------------
// Read-write access to the order form state for the `bobbyr` console
// surface. Each mirrors the corresponding GUI click; the order page does
// not need to be the active laptop screen, but the engine will redraw
// next frame if it is. See docs/bobbyr.md.

struct BobbyR_OrderState
{
	INT8   selectedCity;     // gbSelectedCity, -1 if none
	UINT8  selectedSpeed;    // gubSelectedLight: 0=overnight, 1=2-day, 2=standard
	UINT32 subtotal;         // sum(unit price * qty) across cart lines
	UINT32 packageWeight;    // tenths of a pound (engine unit)
	UINT32 shippingCost;     // dollars at current city + speed (0 if no city)
	UINT32 grandTotal;       // subtotal + shippingCost
	bool   anyInCart;        // any cart line has ubNumberPurchased > 0
	bool   confirmModalUp;   // gfDrawConfirmOrderGrpahic
};
BobbyR_OrderState BobbyR_GetOrderState();

// Set the destination city (locationId in the shipping destinations
// vector). Returns false on out-of-range; clears the dropdown either way.
bool BobbyR_SetSelectedCity(INT8 cityId);

// Set the shipping speed (0=overnight, 1=2-day, 2=standard).
// Returns false on out-of-range.
bool BobbyR_SetSelectedSpeed(UINT8 speed);

// Mirrors the Clear Order button (BtnBobbyRClearOrderCallback).
void BobbyR_ClearCart();

// Mirrors the YES path of the confirm message box: validates funds /
// city / cart, commits the shipment, deducts cash, raises the post-
// order overlay. Returns the index in gpNewBobbyrShipments on success
// or -1 on failure (with `failReason` populated).
INT32 BobbyR_PlaceOrder(ST::string& failReason);

// Mirrors the secondary callback on the post-order confirm region:
// dismiss the overlay without leaving the order page.
void BobbyR_DismissConfirmOverlay();

#endif
