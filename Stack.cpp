#include <stdio.h>
#include <malloc.h>
#include "Stack.h"
#include "Hash.h"
#include "ColorPrint.h"


static const size_t MinCapacity = 1<<3;
static const size_t MaxCapacity = 1<<21;

static const unsigned int CapPushReallocCoef = 2;
static const unsigned int CapPopReallocCoef  = 4;

static const size_t DefaultStackCapacity = 1<<4;

ON_STACK_DATA_CANARY
(
typedef uint64_t DataCanary_t;
)
ON_STACK_CANARY
(
static const StackCanary_t LeftStackCanary  = 0xDEEADDEADDEAD;
static const StackCanary_t RightStackCanary = 0xDEADDEDDEADED;
)
ON_STACK_DATA_CANARY
(
static const DataCanary_t LeftDataCanary    = 0xEDADEDAEDADEDA;
static const DataCanary_t RightDataCanary   = 0xDEDDEADDEDDEAD;
)
ON_STACK_HASH
(
static const uint64_t DefaultStackHash = 538176576;
)
ON_STACK_DATA_POISON
(
static const StackElem_t Poison = 0xDEEEEEAD;
)

static size_t GetNewCapacity      (size_t Capacity); 
static size_t GetNewCtorCapacity  (size_t StackDataSize);
static size_t GetNewPushCapacity  (const Stack_t* Stack);
static size_t GetNewPopCapacity   (const Stack_t* Stack);

static StackErrorType CtorCalloc   (Stack_t* Stack, StackErrorType* Err, size_t StackDataSize);
static StackErrorType DtorFreeData (Stack_t* Stack, StackErrorType* Err);
static StackErrorType PushRealloc  (Stack_t* Stack, StackErrorType* Err);
static StackErrorType PopRealloc   (Stack_t* Stack, StackErrorType* Err);

ON_STACK_DATA_CANARY
(
static DataCanary_t   GetLeftDataCanary     (const Stack_t* Stack);
static DataCanary_t   GetRightDataCanary    (const Stack_t* Stack);
static void           SetLeftDataCanary     (Stack_t* Stack);
static void           SetRightDataCanary    (Stack_t* Stack);
static StackErrorType MoveDataToLeftCanary  (Stack_t* Stack, StackErrorType* Err);
static StackErrorType MoveDataToFirstElem   (Stack_t* Stack, StackErrorType* Err);
)

ON_STACK_DATA_HASH
(
static uint64_t CalcDataHash(const Stack_t* Stack);
)
ON_STACK_HASH
(
static uint64_t CalcStackHash (Stack_t* Stack);
)

static StackErrorType Verif       (Stack_t* Stack, StackErrorType* Error ON_STACK_DEBUG(, const char* File, int Line, const char* Func));
static void           PrintError  (StackErrorType Error);
static void           PrintPlace  (const char* File, int Line, const char* Function);
ON_STACK_DEBUG
(
static void           ErrPlaceCtor (StackErrorType* Err, const char* File, int Line, const char* Func);
)
//-------------------------------------------------------------------------------------------------------------------------------------------------------

StackErrorType StackCtor(Stack_t* Stack ON_STACK_DEBUG(, const char* File, int Line, const char* Func, const char* Name))
{
    StackErrorType Err = {};

    Stack->Size = 0;

    Stack->Capacity = GetNewCtorCapacity(DefaultStackCapacity);
    CtorCalloc(Stack, &Err, DefaultStackCapacity);
    ON_STACK_CANARY
    (
    Stack->LeftStackCanary  = LeftStackCanary;
    Stack->RightStackCanary = RightStackCanary;
    )
    ON_STACK_DATA_CANARY
    (
    SetLeftDataCanary (Stack);
    SetRightDataCanary(Stack);
    )
    ON_STACK_DATA_POISON
    (
    for (size_t Data_i = 0; Data_i < Stack->Capacity; Data_i++)
    {
        Stack->Data[Data_i] = Poison;
    }
    )
    ON_STACK_DEBUG
    (
    Stack->Var.File = File;
    Stack->Var.Line = Line;
    Stack->Var.Func = Func;
    Stack->Var.Name = Name;
    )
    ON_STACK_DATA_HASH(Stack->DataHash  = CalcDataHash(Stack);)
    ON_STACK_HASH(Stack->StackHash = CalcStackHash(Stack);)

    return STACK_VERIF(Stack, Err);
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------

StackErrorType StackDtor(Stack_t* Stack)
{
    StackErrorType Err = {};
    DtorFreeData(Stack, &Err);
    Stack->Data     = NULL;
    Stack->Capacity = 0;
    Stack->Size     = 0;
    return Err;
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------

StackErrorType StackPush(Stack_t*  Stack, StackElem_t PushElem)
{
    StackErrorType Err = {};
    STACK_RETURN_IF_ERR_OR_WARN(Stack, Err);

    if (Stack->Size + 1 > MaxCapacity)
    {
        Err.Warning.PushInFullStack = 1;
        Err.IsWarning = 1;
        return STACK_VERIF(Stack, Err);
    }

    Stack->Size++;

    if (Stack->Size <= Stack->Capacity)
    {
        Stack->Data[Stack->Size - 1] = PushElem;
    
        ON_STACK_DATA_HASH(Stack->DataHash  = CalcDataHash(Stack);)
        ON_STACK_HASH(Stack->StackHash = CalcStackHash(Stack);)

        return STACK_VERIF(Stack, Err);
    }

    Stack->Capacity = GetNewPushCapacity(Stack);
    PushRealloc(Stack, &Err);

    if (Err.IsFatalError == 1)
    {
        return STACK_VERIF(Stack, Err);
    }

    Stack->Data[Stack->Size - 1] = PushElem;

    ON_STACK_DATA_CANARY(SetRightDataCanary(Stack);)

    ON_STACK_DATA_POISON
    (
    for (size_t Data_i = Stack->Size; Data_i < Stack->Capacity; Data_i++)
    {
        Stack->Data[Data_i] = Poison;
    }
    )

    ON_STACK_DATA_HASH(Stack->DataHash  = CalcDataHash(Stack);)
    ON_STACK_HASH(Stack->StackHash = CalcStackHash(Stack);)
    
    if (Stack->Capacity == MaxCapacity)
    {
        Err.Warning.TooBigCapacity = 1;
        Err.IsWarning = 1;
    }
    return STACK_VERIF(Stack, Err);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

StackErrorType StackPop(Stack_t* Stack, StackElem_t* PopElem)
{
    StackErrorType Err = {};
    STACK_RETURN_IF_ERR_OR_WARN(Stack, Err);

    if (Stack->Size == 0)
    {
        Err.Warning.PopInEmptyStack = 1;
        Err.IsWarning = 1;
        return STACK_VERIF(Stack, Err);
    }

    Stack->Size--;

    *PopElem = Stack->Data[Stack->Size];

    ON_STACK_DATA_POISON(Stack->Data[Stack->Size] = Poison;)
    ON_STACK_DATA_HASH(Stack->DataHash  = CalcDataHash(Stack);)
    ON_STACK_HASH(Stack->StackHash = CalcStackHash(Stack);)

    if (Stack->Size * CapPopReallocCoef > Stack->Capacity)
    {
        return STACK_VERIF(Stack, Err);
    }

    Stack->Capacity = GetNewPopCapacity(Stack);
    PopRealloc(Stack, &Err);

    if (Err.IsFatalError == 1)
    {
        return STACK_VERIF(Stack, Err);
    }

    ON_STACK_DATA_CANARY(SetRightDataCanary(Stack);)
    ON_STACK_DATA_HASH(Stack->DataHash  = CalcDataHash(Stack);)
    ON_STACK_HASH(Stack->StackHash = CalcStackHash(Stack);)

    return STACK_VERIF(Stack, Err);
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------

StackErrorType PrintStack(Stack_t* Stack)
{
    StackErrorType Err = {};
    STACK_RETURN_IF_ERR_OR_WARN(Stack, Err);

    printf("\nStack:\n");
    for (size_t Stack_i = 0; Stack_i < Stack->Size; Stack_i++)
    {
        printf("%d ", Stack->Data[Stack_i]);
    }
    printf("\nStack end\n\n");

    return STACK_VERIF(Stack, Err);
}

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------

StackErrorType PrintLastStackElem(Stack_t* Stack)
{
    StackErrorType Err = {};
    STACK_RETURN_IF_ERR_OR_WARN(Stack, Err);

    StackElem_t PopElem = 0;
    STACK_ASSERT(StackPop(Stack, &PopElem));
    COLOR_PRINT(VIOLET, "%d\n", PopElem);
    return STACK_VERIF(Stack, Err);
}

//---------------------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------------------------

ON_STACK_HASH
(
static uint64_t CalcStackHash(Stack_t* Stack)
{
    const uint64_t StackHashCopy = Stack->StackHash;
    Stack->StackHash             = DefaultStackHash;
    uint64_t NewStackHash        = Hash(Stack, 1, sizeof(Stack_t));
    Stack->StackHash             = StackHashCopy;
    return NewStackHash;
}
)

//----------------------------------------------------------------------------------------------------------------------------------------------------

ON_STACK_DATA_HASH
(
static uint64_t CalcDataHash(const Stack_t* Stack)
{
    return Hash(Stack->Data, Stack->Capacity, sizeof(StackElem_t));
}
)

//----------------------------------------------------------------------------------------------------------------------------------------------------

ON_STACK_DATA_CANARY
(
static DataCanary_t GetLeftDataCanary(const Stack_t* Stack)
{
    return *(DataCanary_t*)((char*)Stack->Data - 1 * sizeof(DataCanary_t));
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------

static DataCanary_t GetRightDataCanary(const Stack_t* Stack)
{
    return *(DataCanary_t*)((char*)Stack->Data + Stack->Capacity * sizeof(StackElem_t));
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static void SetLeftDataCanary(Stack_t* Stack)
{
    *(DataCanary_t*)((char*)Stack->Data - 1 * sizeof(DataCanary_t)) = LeftDataCanary;
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static void SetRightDataCanary(Stack_t* Stack)
{
    *(DataCanary_t*)((char*)Stack->Data + Stack->Capacity * sizeof(StackElem_t)) = RightDataCanary;
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType MoveDataToLeftCanary(Stack_t* Stack, StackErrorType* Err)
{
    Stack->Data = (StackElem_t*)((char*)Stack->Data - 8 * sizeof(char));

    if (Stack->Data == NULL)
    {
        Err->FatalError.DataNull = 1;
        Err->IsFatalError = 1;
    }
    return *Err;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType MoveDataToFirstElem(Stack_t* Stack, StackErrorType* Err)
{
    Stack->Data = (StackElem_t*)((char*)Stack->Data + 8 * sizeof(char));

    if (Stack->Data == NULL)
    {
        Err->FatalError.DataNull = 1;
        Err->IsFatalError = 1;
    }
    return *Err;
}
)
//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static size_t GetNewCtorCapacity(const size_t StackDataSize)
{
    size_t Temp = (StackDataSize > MinCapacity) ? StackDataSize : MinCapacity;
    return GetNewCapacity(Temp);
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static size_t GetNewPushCapacity(const Stack_t* Stack)
{
    size_t NewCapacity = (Stack->Capacity * CapPushReallocCoef);
    size_t Temp = NewCapacity < MaxCapacity ? NewCapacity : MaxCapacity;
    return GetNewCapacity(Temp);
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static size_t GetNewPopCapacity(const Stack_t* Stack)
{
    size_t NewCapacity = (Stack->Capacity / CapPopReallocCoef);
    size_t Temp = (NewCapacity > MinCapacity) ? NewCapacity : MinCapacity;
    return GetNewCapacity(Temp);
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static size_t GetNewCapacity(size_t Capacity)
{
    ON_STACK_DATA_CANARY
    (
    size_t DataCanarySize = sizeof(DataCanary_t);
    size_t Temp = (Capacity % DataCanarySize == 0) ? 0 : DataCanarySize - Capacity % DataCanarySize;
    Capacity += Temp;
    )
    return Capacity;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType PushRealloc(Stack_t* Stack, StackErrorType* Err)
{
    ON_STACK_DATA_CANARY
    (
    MoveDataToLeftCanary(Stack, Err);
    )
    Stack->Data = (StackElem_t*) realloc(Stack->Data, Stack->Capacity * sizeof(StackElem_t) ON_STACK_DATA_CANARY(+ 2 * sizeof(DataCanary_t)));
    if (Stack->Data == NULL)
    {
        Err->FatalError.ReallocPushNull = 1;
        Err->IsFatalError = 1;
        return *Err;
    }
    ON_STACK_DATA_CANARY
    (
    MoveDataToFirstElem(Stack, Err);
    )
    return *Err; 
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType PopRealloc(Stack_t* Stack, StackErrorType* Err)
{
    ON_STACK_DATA_CANARY
    (
    MoveDataToLeftCanary(Stack, Err);
    )
    Stack->Data = (StackElem_t*) realloc(Stack->Data, Stack->Capacity * sizeof(StackElem_t) ON_STACK_DATA_CANARY(+ 2 * sizeof(DataCanary_t)));
    if (Stack->Data == NULL)
    {
        Err->FatalError.ReallocPopNull = 1;
        Err->IsFatalError = 1;
        return *Err;
    }
    ON_STACK_DATA_CANARY
    (
    MoveDataToFirstElem(Stack, Err);
    )
    return *Err;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType CtorCalloc(Stack_t* Stack, StackErrorType* Err, size_t StackDataSize)
{
    Stack->Data = (StackElem_t*) calloc (Stack->Capacity * sizeof(StackElem_t) ON_STACK_DATA_CANARY(+ 2 * sizeof(DataCanary_t)), sizeof(char));
    if (Stack->Data == NULL)
    {
        Err->FatalError.DataNull = 1;
        Err->IsFatalError = 1;
        return *Err;
    }
    ON_STACK_DATA_CANARY
    (
    MoveDataToFirstElem(Stack, Err);
    )
    return *Err;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType DtorFreeData(Stack_t* Stack, StackErrorType* Err)
{
    ON_STACK_DATA_CANARY
    (
    MoveDataToLeftCanary(Stack, Err);
    )
    free(Stack->Data);
    return *Err;
}


//--------------------------------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------

static StackErrorType Verif(Stack_t* Stack, StackErrorType* Error ON_STACK_DEBUG(, const char* File, int Line, const char* Func))
{
    ON_STACK_DEBUG
    (
    ErrPlaceCtor(Error, File, Line, Func);
    )
 
    if (Stack == NULL)
    {
        Error->FatalError.StackNull = 1;
        Error->IsFatalError = 1;
        return *Error;
    }  
    else
    {
        Error->FatalError.StackNull = 0;   
    }

    if (Stack->Data == NULL)
    {
        Error->FatalError.DataNull = 1;
        Error->IsFatalError = 1;
        return *Error;
    }
    else
    {
        Error->FatalError.DataNull = 0;
    }

    ON_STACK_CANARY
    (
    if (Stack->LeftStackCanary != LeftStackCanary)
    {
        Error->FatalError.LeftStackCanaryChanged = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.LeftStackCanaryChanged = 0;
    }

    if (Stack->RightStackCanary != RightStackCanary)
    {
        Error->FatalError.RightStackCanaryChanged = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.RightStackCanaryChanged = 0;
    }
    )

    ON_STACK_DATA_CANARY
    (
    if (GetLeftDataCanary(Stack) != LeftDataCanary)
    {
        Error->FatalError.LeftDataCanaryChanged = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.LeftDataCanaryChanged = 0;
    }

    if (GetRightDataCanary(Stack) != RightDataCanary)
    {
        Error->FatalError.RightDataCanaryChanged = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.RightDataCanaryChanged = 0;
    }
    )

    ON_STACK_DEBUG
    (
    if (Stack->Size > Stack->Capacity)
    {
        Error->FatalError.SizeBiggerCapacity = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.SizeBiggerCapacity = 0;   
    }

    if (Stack->Capacity < MinCapacity)
    {
        Error->FatalError.CapacitySmallerMin = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.CapacitySmallerMin = 0;
    }

    if (Stack->Capacity > MaxCapacity)
    {
        Error->FatalError.CapacityBiggerMax = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.CapacityBiggerMax = 0;
    }
    
    if (Stack->Var.File == NULL)
    {
        Error->FatalError.CtorStackFileNull = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.CtorStackFileNull = 0;   
    }

    if (Stack->Var.Func == NULL)
    {
        Error->FatalError.CtorStackFuncNull = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.CtorStackFuncNull = 0;
    }

    if (Stack->Var.Name == NULL)
    {
        Error->FatalError.CtorStackNameNull = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.CtorStackNameNull = 0;
    }

    if (Stack->Var.Line < 0)
    {
        Error->FatalError.CtorStackLineNegative = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.CtorStackLineNegative = 0;
    }
    )

    ON_STACK_DATA_POISON
    (
    bool WasNotPosion = false;
    for (size_t Data_i = Stack->Size; Data_i < Stack->Capacity; Data_i++)
    {
        if (Stack->Data[Data_i] != Poison)
        {
            Error->FatalError.DataElemBiggerSizeNotPoison = 1;
            Error->IsFatalError = 1;
            WasNotPosion = true;
            break;
        }
    }

    if (!WasNotPosion)
    {
        Error->FatalError.DataElemBiggerSizeNotPoison = 0;
    }
    )
    
    ON_STACK_DATA_HASH
    (
    if (CalcDataHash(Stack) != Stack->DataHash)
    {
        Error->FatalError.DataHashChanged = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.DataHashChanged = 0;
    }
    )

    ON_STACK_HASH
    (
    if (CalcStackHash(Stack) != Stack->StackHash)
    {
        Error->FatalError.StackHashChanged = 1;
        Error->IsFatalError = 1;
    }
    else
    {
        Error->FatalError.StackHashChanged = 0;
    }
    )
    return *Error;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static void PrintError(StackErrorType Error)
{
    if (Error.IsWarning == 0 && Error.IsFatalError == 0)
    {
        return;
    }
    
    if (Error.IsWarning == 1)
    {
        if (Error.Warning.PopInEmptyStack == 1)
        {
            COLOR_PRINT(YELLOW, "Warning: make pop, but Stack is empty.\n");
            COLOR_PRINT(YELLOW, "Pop will not change PopElem.\n");
        }

        if (Error.Warning.TooBigCapacity == 1)
        {
            COLOR_PRINT(YELLOW, "Warning: to big Data size.\n");
            COLOR_PRINT(YELLOW, "Capacity have a max allowed value.\n");
        }

        if (Error.Warning.PushInFullStack == 1)
        {
            COLOR_PRINT(YELLOW, "Warning: made Push in full Stack.\n");
            COLOR_PRINT(YELLOW, "Stack won't change.\n");
        }
    }

    if (Error.IsFatalError == 1)
    {
        if (Error.FatalError.StackNull == 1)
        {
            COLOR_PRINT(RED, "Error: Right Data Canary was changed.\n");
            OFF_STACK_DEBUG(COLOR_PRINT(RED, "!Stack Data can be incorrect!\n"));
        }

        if (Error.FatalError.DataNull == 1)
        {
            COLOR_PRINT(RED, "Error: Data is NULL.\n");
        }

        if (Error.FatalError.CallocCtorNull == 1)
        {
            COLOR_PRINT(RED, "Error: failed to allocate memory in Ctor.\n");
        }

        if (Error.FatalError.ReallocPushNull == 1)
        {
            COLOR_PRINT(RED, "Error: failed to reallocate memory in Push.\n");
        }

        if (Error.FatalError.ReallocPopNull == 1)
        {
            COLOR_PRINT(RED, "Error: failed to free memory in Pop.\n");
        }

        ON_STACK_CANARY
        (
        if (Error.FatalError.LeftStackCanaryChanged == 1)
        {
            COLOR_PRINT(RED, "Error: Left Stack Canary was changed.\n");
            OFF_STACK_DEBUG(COLOR_PRINT(RED, "!Stack Data can be incorrect!\n"));
        }
        
        if (Error.FatalError.RightStackCanaryChanged == 1)
        {
            COLOR_PRINT(RED, "Error: Right Stack Canary was changed.\n");
            OFF_STACK_DEBUG(COLOR_PRINT(RED, "!Stack Data can be incorrect!\n"));
        }
        )
        ON_STACK_DATA_CANARY
        (
        if (Error.FatalError.LeftDataCanaryChanged == 1)
        {
            COLOR_PRINT(RED, "Error: Left Data Canary was changed.\n");
            OFF_STACK_DEBUG(COLOR_PRINT(RED, "!Stack Data can be incorrect!\n"));
        }

        if (Error.FatalError.RightDataCanaryChanged == 1)
        {
            COLOR_PRINT(RED, "Error: Right Data Canary was changed.\n");
            OFF_STACK_DEBUG(COLOR_PRINT(RED, "!Stack Data can be incorrect!\n"));
        }
        )

        ON_STACK_DATA_POISON
        (
        if (Error.FatalError.DataElemBiggerSizeNotPoison == 1)
        {
            COLOR_PRINT(RED, "Error: After Size in Data is not Poison elem.\n");
        }
        )

        ON_STACK_DEBUG
        (
        if (Error.FatalError.SizeBiggerCapacity == 1)
        {
            COLOR_PRINT(RED, "Error: Size > Capacity.\n");
        }
        
        if (Error.FatalError.CapacityBiggerMax == 1)
        {
            COLOR_PRINT(RED, "Error: Capacity > MaxCapacity.\n");
        }

        if (Error.FatalError.CapacitySmallerMin == 1)
        {
            COLOR_PRINT(RED, "Error: Capacity < MinCapacity.\n");
        }

        if (Error.FatalError.CtorStackFileNull == 1)
        {
            COLOR_PRINT(RED, "Error: Stack ctor init file is NULL.\n");
        }

        if (Error.FatalError.CtorStackFuncNull == 1)
        {
            COLOR_PRINT(RED, "Error: Stack ctor init func is NULL.\n");
        }
        
        if (Error.FatalError.CtorStackLineNegative == 1)
        {
            COLOR_PRINT(RED, "Error: Stack ctor init line is negative or 0.\n");
        }
        )

        ON_STACK_DATA_HASH
        (
        if (Error.FatalError.DataHashChanged == 1)
        {
            COLOR_PRINT(RED, "Error: Data Hash is incorrect.\n");
        }
        )

        ON_STACK_HASH
        (
        if (Error.FatalError.StackHashChanged == 1)
        {
            COLOR_PRINT(RED, "Error: Stack Hash is incorrect.\n");
        }
        )
    }
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

ON_STACK_DEBUG
(
// void StackDump(const Stack_t* Stack, const char* File, int Line, const char* Func)
// {   
//     COLOR_PRINT(GREEN, "\nStackDump BEGIN\n\n");

//     #define DANG_DUMP

//     #ifndef DANG_DUMP
//         ON_STACK_HASH
//         (
//         if (Stack->StackHash != CalcStackHash(Stack))
//         {
//             COLOR_PRINT(RED, "Incorrect hash.\n");
//             COLOR_PRINT(WHITE, "Correct hash    = %d\n", CalcStackHash(Stack));
//             COLOR_PRINT(WHITE, "Stack.StackHash = %d\n", Stack->StackHash);
//             return;
//         }
//         )
//     #else
//         COLOR_PRINT(YELLOW, "WARNING: StackDump is in dangerous mode.\n");
//         COLOR_PRINT(YELLOW, "Undefined bahavior is possible.\n\n");
//     #endif

//     COLOR_PRINT(VIOLET, "Where StackDump made:\n");
//     PrintPlace(File, Line, Func);

//     COLOR_PRINT(VIOLET, "Stack data during Ctor:\n");

//     if (Stack == NULL)
//     {
//         COLOR_PRINT(RED, "Stack = NULL\n");
//         return;
//     }

//     if (&Stack->Var == NULL)
//     {
//         COLOR_PRINT(RED, "Stack.Var = NULL\n");
//         return;
//     }

//     if (Stack->Var.Name == NULL)
//     {
//         COLOR_PRINT(RED, "Stack.Var.Name = NULL\n");
//     }
//     else
//     {
//         COLOR_PRINT(WHITE, "Name [%s]\n", Stack->Var.Name);
//     }

//     if (Stack->Var.File == NULL)
//     {
//         COLOR_PRINT(RED, "Stack.Var.File = NULL\n");
//     }
//     else
//     {
//         COLOR_PRINT(WHITE, "File [%s]\n", Stack->Var.File);
//     }

//     if (Stack->Var.Func == NULL)
//     {
//         COLOR_PRINT(RED, "Stack.Var.Func is NULL\n");
//     }
//     else
//     {
//         COLOR_PRINT(WHITE, "Func [%s]\n", Stack->Var.Func);
//     }

//     COLOR_PRINT(WHITE, "Line [%d]\n\n", Stack->Var.Line);
    
//     if (Stack->Data == NULL)
//     {
//         COLOR_PRINT(RED, "Stack.Data = NULL");
//         return;
//     }

//     COLOR_PRINT(VIOLET, "&Stack = 0x%p\n", Stack);
//     COLOR_PRINT(VIOLET, "&Data  = 0x%p\n\n", Stack->Data);

//     ON_STACK_CANARY
//     (
//     COLOR_PRINT(YELLOW, "Left  Stack Canary = 0x%llx = %llu\n",   Stack->LeftStackCanary,    Stack->LeftStackCanary);
//     COLOR_PRINT(YELLOW, "Right Stack Canary = 0x%llx = %llu\n\n", Stack->RightStackCanary,   Stack->RightStackCanary);
//     )
//     ON_STACK_DATA_CANARY
//     (
//     COLOR_PRINT(YELLOW, "Left  Data  Canary = 0x%llx = %llu\n",   GetLeftDataCanary(Stack),  GetLeftDataCanary(Stack));
//     COLOR_PRINT(YELLOW, "Right Data  Canary = 0x%llx = %llu\n\n", GetRightDataCanary(Stack), GetRightDataCanary(Stack));
//     )

//     ON_STACK_HASH (COLOR_PRINT(BLUE, "Stack Hash = %llu\n",   Stack->StackHash);)
//     ON_STACK_DATA_HASH (COLOR_PRINT(BLUE, "Data  Hash = %llu\n\n", Stack->DataHash);)

//     COLOR_PRINT(CYAN, "Size = %u\n", Stack->Size);
//     COLOR_PRINT(CYAN, "Capacity = %u\n\n", Stack->Capacity);


//     ON_STACK_DATA_POISON (COLOR_PRINT(GREEN, "Poison = 0x%x = %d\n\n", Poison, Poison);)

//     COLOR_PRINT(BLUE, "Data = \n{\n");
//     for (size_t Data_i = 0; Data_i < Stack->Size; Data_i++)
//     {
//         COLOR_PRINT(BLUE, "*[%2u] %d\n", Data_i, Stack->Data[Data_i]);
//     }

//     for (size_t Data_i = Stack->Size; Data_i < Stack->Capacity; Data_i++)
//     {
//         COLOR_PRINT(CYAN, " [%2u] 0x%x\n", Data_i, Stack->Data[Data_i]);   
//     }
//     COLOR_PRINT(BLUE, "};\n\n");


//     COLOR_PRINT(VIOLET, "Data ptrs = \n{\n");
//     for (size_t Data_i = 0; Data_i < Stack->Size; Data_i++)
//     {
//         COLOR_PRINT(VIOLET, "*[%2u] 0x%p\n", Data_i, &Stack->Data[Data_i]);
//     }

//     for (size_t Data_i = Stack->Size; Data_i < Stack->Capacity; Data_i++)
//     {
//         COLOR_PRINT(CYAN, " [%2u] 0x%p\n", Data_i, &Stack->Data[Data_i]);   
//     }
//     COLOR_PRINT(VIOLET, "};\n");

//     COLOR_PRINT(GREEN, "\n\nStackDump END\n\n");
//     return;
// }

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static void ErrPlaceCtor(StackErrorType* Err, const char* File, int Line, const char* Func)
{
    Err->File = File;
    Err->Line = Line;
    Err->Func = Func;
    return;
}
)

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

static void PrintPlace(const char* File, int Line, const char* Function)
{
    COLOR_PRINT(WHITE, "File [%s]\nLine [%d]\nFunc [%s]\n", File, Line, Function);
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------

void StackAssertPrint(StackErrorType Err, const char* File, int Line, const char* Func)
{
    if (Err.IsFatalError == 1 || Err.IsWarning == 1) 
    {
        COLOR_PRINT(RED, "Assert made in:\n");
        PrintPlace(File, Line, Func);
        PrintError(Err);
        ON_STACK_DEBUG
        (
        PrintPlace(Err.File, Err.Line, Err.Func);
        )
        printf("\n");
    }
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------
