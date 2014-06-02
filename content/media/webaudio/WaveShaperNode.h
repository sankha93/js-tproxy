/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WaveShaperNode_h_
#define WaveShaperNode_h_

#include "AudioNode.h"
#include "mozilla/dom/WaveShaperNodeBinding.h"
#include "mozilla/dom/TypedArray.h"

namespace mozilla {
namespace dom {

class AudioContext;

class WaveShaperNode : public AudioNode
{
public:
  explicit WaveShaperNode(AudioContext *aContext);
  virtual ~WaveShaperNode();

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(WaveShaperNode, AudioNode)

  virtual JSObject* WrapObject(JSContext *aCx) MOZ_OVERRIDE;

  JSObject* GetCurve(JSContext* aCx) const
  {
    return mCurve;
  }
  void SetCurve(const Nullable<Float32Array>& aData);

  OverSampleType Oversample() const
  {
    return mType;
  }
  void SetOversample(OverSampleType aType);

  virtual size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const MOZ_OVERRIDE
  {
    // Possibly track in the future:
    // - mCurve
    return AudioNode::SizeOfExcludingThis(aMallocSizeOf);
  }

  virtual const char* NodeType() const
  {
    return "WaveShaperNode";
  }

  virtual size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const MOZ_OVERRIDE
  {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

private:
  void ClearCurve();

private:
  JS::Heap<JSObject*> mCurve;
  OverSampleType mType;
};

}
}

#endif