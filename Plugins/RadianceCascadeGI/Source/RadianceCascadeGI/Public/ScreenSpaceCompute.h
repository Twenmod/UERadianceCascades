#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "ScreenSpaceCompute.generated.h"

struct RADIANCECASCADEGI_API FDispatchParams
{
	int X;
	int Y;
	int Z;


	int Input[2];
	int Output;



	FDispatchParams(int x, int y, int z)
		: X(x)
		, Y(y)
		, Z(z)
	{
	}
};

// This is a public interface that we define so outside code can invoke our compute shader.
class RADIANCECASCADEGI_API FComputeShaderInterface
{
public:
	// Executes this shader on the render thread
	static void DispatchRenderThread(
		FRHICommandListImmediate& RHICmdList,
		FDispatchParams Params,
		TFunction<void(int OutputVal)> AsyncCallback
	);

	// Executes this shader on the render thread from the game thread via EnqueueRenderThreadCommand
	static void DispatchGameThread(
		FDispatchParams Params,
		TFunction<void(int OutputVal)> AsyncCallback
	)
	{
		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
			[Params, AsyncCallback](FRHICommandListImmediate& RHICmdList)
			{
				DispatchRenderThread(RHICmdList, Params, AsyncCallback);
			});
	}

	// Dispatches this shader. Can be called from any thread
	static void Dispatch(
		FDispatchParams Params,
		TFunction<void(int OutputVal)> AsyncCallback
	)
	{
		if (IsInRenderingThread())
		{
			DispatchRenderThread(GetImmediateCommandList_ForRenderCommand(), Params, AsyncCallback);
		}
		else
		{
			DispatchGameThread(Params, AsyncCallback);
		}
	}
};



DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRadianceCascadeGI_AsyncExecutionCompleted, const int, Value);


UCLASS()
class RADIANCECASCADEGI_API URadianceCascadeGI_AsyncExecution : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:

	// Execute the actual load
	virtual void Activate() override
	{
		// Create a dispatch parameters struct and fill it the input array with our args
		FDispatchParams Params(1, 1, 1);
		Params.Input[0] = Arg1;
		Params.Input[1] = Arg2;

		// Dispatch the compute shader and wait until it completes
		FComputeShaderInterface::Dispatch(Params, [this](int OutputVal)
			{
				this->Completed.Broadcast(OutputVal);
			});
	}



	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", Category = "ComputeShader", WorldContext = "WorldContextObject"))
	static URadianceCascadeGI_AsyncExecution* ExecuteBaseComputeShader(UObject* WorldContextObject, int Arg1, int Arg2)
	{
		URadianceCascadeGI_AsyncExecution* Action = NewObject<URadianceCascadeGI_AsyncExecution>();
		Action->Arg1 = Arg1;
		Action->Arg2 = Arg2;
		Action->RegisterWithGameInstance(WorldContextObject);

		return Action;
	}

	UPROPERTY(BlueprintAssignable)
	FOnRadianceCascadeGI_AsyncExecutionCompleted Completed;


	int Arg1;
	int Arg2;

};