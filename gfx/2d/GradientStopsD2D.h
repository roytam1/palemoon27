/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_GRADIENTSTOPSD2D_H_
#define MOZILLA_GFX_GRADIENTSTOPSD2D_H_

#include "2D.h"

#include <d2d1.h>

namespace mozilla {
namespace gfx {

class GradientStopsD2D : public GradientStops
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GradientStopsD2D)
#ifdef USE_D2D1_1
  GradientStopsD2D(ID2D1GradientStopCollection *aStopCollection, ID3D11Device *aDevice)
    : mStopCollection(aStopCollection)
    , mDevice(aDevice)
  {}
#else
  GradientStopsD2D(ID2D1GradientStopCollection *aStopCollection)
    : mStopCollection(aStopCollection)
  {}
#endif

  virtual BackendType GetBackendType() const { return BackendType::DIRECT2D; }

#ifdef USE_D2D1_1
  virtual bool IsValid() const final{ return mDevice == Factory::GetDirect3D11Device(); }
#endif

private:
  friend class DrawTargetD2D;
  friend class DrawTargetD2D1;

  mutable RefPtr<ID2D1GradientStopCollection> mStopCollection;
#ifdef USE_D2D1_1
  RefPtr<ID3D11Device> mDevice;
#endif
};

}
}

#endif /* MOZILLA_GFX_GRADIENTSTOPSD2D_H_ */
