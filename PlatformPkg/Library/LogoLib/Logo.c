
#include <PiDxe.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/OEMBadging.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/UgaDraw.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/PcdLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

#include <IndustryStandard/Bmp.h>
#include <Protocol/BootLogo.h>
#include <Protocol/JpegDecoder.h>

/**
  Convert a *.BMP graphics image to a GOP blt buffer. If a NULL Blt buffer
  is passed in a GopBlt buffer will be allocated by this routine. If a GopBlt
  buffer is passed in it will be used if it is big enough.

  @param  BmpImage      Pointer to BMP file
  @param  BmpImageSize  Number of bytes in BmpImage
  @param  GopBlt        Buffer containing GOP version of BmpImage.
  @param  GopBltSize    Size of GopBlt in bytes.
  @param  PixelHeight   Height of GopBlt/BmpImage in pixels
  @param  PixelWidth    Width of GopBlt/BmpImage in pixels

  @retval EFI_SUCCESS           GopBlt and GopBltSize are returned.
  @retval EFI_UNSUPPORTED       BmpImage is not a valid *.BMP image
  @retval EFI_BUFFER_TOO_SMALL  The passed in GopBlt buffer is not big enough.
                                GopBltSize will contain the required size.
  @retval EFI_OUT_OF_RESOURCES  No enough buffer to allocate.

**/
EFI_STATUS
ConvertBmpToGopBlt (
  IN     VOID      *BmpImage,
  IN     UINTN     BmpImageSize,
  IN OUT VOID      **GopBlt,
  IN OUT UINTN     *GopBltSize,
     OUT UINTN     *PixelHeight,
     OUT UINTN     *PixelWidth
  )
{
  UINT8                         *Image;
  UINT8                         *ImageHeader;
  BMP_IMAGE_HEADER              *BmpHeader;
  BMP_COLOR_MAP                 *BmpColorMap;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Blt;
  UINT64                        BltBufferSize;
  UINTN                         Index;
  UINTN                         Height;
  UINTN                         Width;
  UINTN                         ImageIndex;
  BOOLEAN                       IsAllocated;

  BmpHeader = (BMP_IMAGE_HEADER *) BmpImage;

  if (BmpHeader->CharB != 'B' || BmpHeader->CharM != 'M') {
    return EFI_UNSUPPORTED;
  }

  //
  // Doesn't support compress.
  //
  if (BmpHeader->CompressionType != 0) {
    return EFI_UNSUPPORTED;
  }

  //
  // Calculate Color Map offset in the image.
  //
  Image       = BmpImage;
  BmpColorMap = (BMP_COLOR_MAP *) (Image + sizeof (BMP_IMAGE_HEADER));

  //
  // Calculate graphics image data address in the image
  //
  Image         = ((UINT8 *) BmpImage) + BmpHeader->ImageOffset;
  ImageHeader   = Image;

  //
  // Calculate the BltBuffer needed size.
  //
  BltBufferSize = MultU64x32 ((UINT64) BmpHeader->PixelWidth, BmpHeader->PixelHeight);
  //
  // Ensure the BltBufferSize * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL) doesn't overflow
  //
  if (BltBufferSize > DivU64x32 ((UINTN) ~0, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL))) {
      return EFI_UNSUPPORTED;
   }
  BltBufferSize = MultU64x32 (BltBufferSize, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

  IsAllocated   = FALSE;
  if (*GopBlt == NULL) {
    //
    // GopBlt is not allocated by caller.
    //
    *GopBltSize = (UINTN) BltBufferSize;
    *GopBlt     = AllocatePool (*GopBltSize);
    IsAllocated = TRUE;
    if (*GopBlt == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  } else {
    //
    // GopBlt has been allocated by caller.
    //
    if (*GopBltSize < (UINTN) BltBufferSize) {
      *GopBltSize = (UINTN) BltBufferSize;
      return EFI_BUFFER_TOO_SMALL;
    }
  }

  *PixelWidth   = BmpHeader->PixelWidth;
  *PixelHeight  = BmpHeader->PixelHeight;

  //
  // Convert image from BMP to Blt buffer format
  //
  BltBuffer = *GopBlt;
  for (Height = 0; Height < BmpHeader->PixelHeight; Height++) {
    Blt = &BltBuffer[(BmpHeader->PixelHeight - Height - 1) * BmpHeader->PixelWidth];
    for (Width = 0; Width < BmpHeader->PixelWidth; Width++, Image++, Blt++) {
      switch (BmpHeader->BitPerPixel) {
      case 1:
        //
        // Convert 1-bit (2 colors) BMP to 24-bit color
        //
        for (Index = 0; Index < 8 && Width < BmpHeader->PixelWidth; Index++) {
          Blt->Red    = BmpColorMap[((*Image) >> (7 - Index)) & 0x1].Red;
          Blt->Green  = BmpColorMap[((*Image) >> (7 - Index)) & 0x1].Green;
          Blt->Blue   = BmpColorMap[((*Image) >> (7 - Index)) & 0x1].Blue;
          Blt++;
          Width++;
        }

        Blt--;
        Width--;
        break;

      case 4:
        //
        // Convert 4-bit (16 colors) BMP Palette to 24-bit color
        //
        Index       = (*Image) >> 4;
        Blt->Red    = BmpColorMap[Index].Red;
        Blt->Green  = BmpColorMap[Index].Green;
        Blt->Blue   = BmpColorMap[Index].Blue;
        if (Width < (BmpHeader->PixelWidth - 1)) {
          Blt++;
          Width++;
          Index       = (*Image) & 0x0f;
          Blt->Red    = BmpColorMap[Index].Red;
          Blt->Green  = BmpColorMap[Index].Green;
          Blt->Blue   = BmpColorMap[Index].Blue;
        }
        break;

      case 8:
        //
        // Convert 8-bit (256 colors) BMP Palette to 24-bit color
        //
        Blt->Red    = BmpColorMap[*Image].Red;
        Blt->Green  = BmpColorMap[*Image].Green;
        Blt->Blue   = BmpColorMap[*Image].Blue;
        break;

      case 24:
        //
        // It is 24-bit BMP.
        //
        Blt->Blue   = *Image++;
        Blt->Green  = *Image++;
        Blt->Red    = *Image;
        break;

      default:
        //
        // Other bit format BMP is not supported.
        //
        if (IsAllocated) {
          FreePool (*GopBlt);
          *GopBlt = NULL;
        }
        return EFI_UNSUPPORTED;
        break;
      };

    }

    ImageIndex = (UINTN) (Image - ImageHeader);
    if ((ImageIndex % 4) != 0) {
      //
      // Bmp Image starts each row on a 32-bit boundary!
      //
      Image = Image + (4 - (ImageIndex % 4));
    }
  }

  return EFI_SUCCESS;
}

/**
  Convert a *.JPG graphics image to a GOP blt buffer. If a NULL Blt buffer
  is passed in a GopBlt buffer will be allocated by this routine. If a GopBlt
  buffer is passed in it will be used if it is big enough.

  @param  JpgImage      Pointer to BMP file
  @param  JpgImageSize  Number of bytes in JpgImage
  @param  GopBlt        Buffer containing GOP version of JPgImage.
  @param  GopBltSize    Size of GopBlt in bytes.
  @param  PixelHeight   Height of GopBlt/JpgImage in pixels
  @param  PixelWidth    Width of GopBlt/JpgImage in pixels

  @retval EFI_SUCCESS           GopBlt and GopBltSize are returned.
  @retval EFI_UNSUPPORTED       JpgImage is not a valid *.JPG image
  @retval EFI_BUFFER_TOO_SMALL  The passed in GopBlt buffer is not big enough.
                                GopBltSize will contain the required size.
  @retval EFI_OUT_OF_RESOURCES  No enough buffer to allocate.

**/
EFI_STATUS
ConvertJpgToGopBlt (
  IN     VOID      *JpgImage,
  IN     UINTN     JpgImageSize,
  IN OUT VOID      **Buffer,
  IN OUT UINTN     *GopBltSize,
     OUT UINTN     *Height,
     OUT UINTN     *Width
  )
{
  EFI_STATUS                Status;
  EFI_JPEG_DECODER_STATUS   JpegDecodeStatus;
  EFI_JPEG_DECODER_PROTOCOL * JpegDecoder ;

  *Buffer = NULL;
  *Width  = 0;
  *Height = 0;
  Status  = gBS->LocateProtocol (&gEfiJpegDecoderProtocolGuid, NULL, (VOID **) &JpegDecoder);
  if(EFI_ERROR( Status )){
    return EFI_UNSUPPORTED;
  }

  Status = JpegDecoder -> DecodeImage(
    JpegDecoder,
    JpgImage,
    JpgImageSize,
    (UINT8**)Buffer,
    GopBltSize,
    Height,
    Width,
    &JpegDecodeStatus
    );

  return Status;
}

/**
  Use SystemTable Conout to stop video based Simple Text Out consoles from going
  to the video device. Put up LogoFile on every video device that is a console.

  @param[in]  LogoFile   File name of logo to display on the center of the screen.

  @retval EFI_SUCCESS     ConsoleControl has been flipped to graphics and logo displayed.
  @retval EFI_UNSUPPORTED Logo not found

**/
EFI_STATUS
EFIAPI
EnableQuietBoot (
  IN  EFI_GUID  *LogoFile
  )
{
  EFI_STATUS                    Status;
  EFI_OEM_BADGING_PROTOCOL      *Badging;
  UINT32                        SizeOfX;
  UINT32                        SizeOfY;
  INTN                          DestX;
  INTN                          DestY;
  UINT8                         *ImageData;
  UINTN                         ImageSize;
  UINTN                         BltSize;
  UINT32                        Instance;
  EFI_BADGING_FORMAT            Format;
  EFI_BADGING_DISPLAY_ATTRIBUTE Attribute;
  UINTN                         CoordinateX;
  UINTN                         CoordinateY;
  UINTN                         Height;
  UINTN                         Width;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Blt;
  EFI_UGA_DRAW_PROTOCOL         *UgaDraw;
  UINT32                        ColorDepth;
  UINT32                        RefreshRate;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput;
  EFI_BOOT_LOGO_PROTOCOL        *BootLogo;
  UINTN                         NumberOfLogos;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *LogoBlt;
  UINTN                         LogoDestX;
  UINTN                         LogoDestY;
  UINTN                         LogoHeight;
  UINTN                         LogoWidth;
  UINTN                         NewDestX;
  UINTN                         NewDestY;
  UINTN                         NewHeight;
  UINTN                         NewWidth;


  UgaDraw = NULL;
  //
  // Try to open GOP first
  //
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **) &GraphicsOutput);
  if (EFI_ERROR (Status) && FeaturePcdGet (PcdUgaConsumeSupport)) {
    GraphicsOutput = NULL;
    //
    // Open GOP failed, try to open UGA
    //
    Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiUgaDrawProtocolGuid, (VOID **) &UgaDraw);
  }
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Try to open Boot Logo Protocol.
  //
  BootLogo = NULL;
  gBS->LocateProtocol (&gEfiBootLogoProtocolGuid, NULL, (VOID **) &BootLogo);

  //
  // Erase Cursor from screen
  //
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  Badging = NULL;
  Status  = gBS->LocateProtocol (&gEfiOEMBadgingProtocolGuid, NULL, (VOID **) &Badging);

  if (GraphicsOutput != NULL) {
    SizeOfX = GraphicsOutput->Mode->Info->HorizontalResolution;
    SizeOfY = GraphicsOutput->Mode->Info->VerticalResolution;

  } else if (UgaDraw != NULL && FeaturePcdGet (PcdUgaConsumeSupport)) {
    Status = UgaDraw->GetMode (UgaDraw, &SizeOfX, &SizeOfY, &ColorDepth, &RefreshRate);
    if (EFI_ERROR (Status)) {
      return EFI_UNSUPPORTED;
    }
  } else {
    return EFI_UNSUPPORTED;
  }

  Blt = NULL;
  NumberOfLogos = 0;
  LogoDestX = 0;
  LogoDestY = 0;
  LogoHeight = 0;
  LogoWidth = 0;
  NewDestX = 0;
  NewDestY = 0;
  NewHeight = 0;
  NewWidth = 0;
  Instance = 0;
  while (1) {
    ImageData = NULL;
    ImageSize = 0;

    if (Badging != NULL) {
      //
      // Get image from OEMBadging protocol.
      //
      Status = Badging->GetImage (
                          Badging,
                          &Instance,
                          &Format,
                          &ImageData,
                          &ImageSize,
                          &Attribute,
                          &CoordinateX,
                          &CoordinateY
                          );
      if (EFI_ERROR (Status)) {
        goto Done;
      }

    } else {
      //
      // Get the specified image from FV.
      //
      Status = GetSectionFromAnyFv (LogoFile, EFI_SECTION_RAW, 0, (VOID **) &ImageData, &ImageSize);
      if (EFI_ERROR (Status)) {
        return EFI_UNSUPPORTED;
      }

      CoordinateX = 0;
      CoordinateY = 0;
      Attribute   = EfiBadgingDisplayAttributeCenter;
      Format      = EfiBadgingFormatUnknown;
    }

    if (Blt != NULL) {
      FreePool (Blt);
    }

    //
    // Try BMP decoder
    //
    Blt    = NULL;
    Width  = 0;
    Height = 0;
    Status = EFI_SUCCESS;
    if(Format == EfiBadgingFormatBMP || Format == EfiBadgingFormatUnknown) {
      Status = ConvertBmpToGopBlt (
                ImageData,
                ImageSize,
                (VOID **) &Blt,
                &BltSize,
                &Height,
                &Width
                );
      if(!EFI_ERROR (Status)) {
        Format  = EfiBadgingFormatBMP;
      }
    }

    if(Format == EfiBadgingFormatJPEG || Format == EfiBadgingFormatUnknown) {
      Status = ConvertJpgToGopBlt (
                ImageData,
                ImageSize,
                (VOID **) &Blt,
                &BltSize,
                &Height,
                &Width
                );
      if(!EFI_ERROR (Status)) {
        Format  = EfiBadgingFormatJPEG;
      }
    }

    if(EFI_ERROR(Status)) {
      FreePool (ImageData);

      if (Badging == NULL) {
        return Status;
      } else {
        continue;
      }
    }

    //
    // Calculate the display position according to Attribute.
    //
    switch (Attribute) {
    case EfiBadgingDisplayAttributeLeftTop:
      DestX = CoordinateX;
      DestY = CoordinateY;
      break;

    case EfiBadgingDisplayAttributeCenterTop:
      DestX = (SizeOfX - Width) / 2;
      DestY = CoordinateY;
      break;

    case EfiBadgingDisplayAttributeRightTop:
      DestX = (SizeOfX - Width - CoordinateX);
      DestY = CoordinateY;;
      break;

    case EfiBadgingDisplayAttributeCenterRight:
      DestX = (SizeOfX - Width - CoordinateX);
      DestY = (SizeOfY - Height) / 2;
      break;

    case EfiBadgingDisplayAttributeRightBottom:
      DestX = (SizeOfX - Width - CoordinateX);
      DestY = (SizeOfY - Height - CoordinateY);
      break;

    case EfiBadgingDisplayAttributeCenterBottom:
      DestX = (SizeOfX - Width) / 2;
      DestY = (SizeOfY - Height - CoordinateY);
      break;

    case EfiBadgingDisplayAttributeLeftBottom:
      DestX = CoordinateX;
      DestY = (SizeOfY - Height - CoordinateY);
      break;

    case EfiBadgingDisplayAttributeCenterLeft:
      DestX = CoordinateX;
      DestY = (SizeOfY - Height) / 2;
      break;

    case EfiBadgingDisplayAttributeCenter:
      DestX = (SizeOfX - Width) / 2;
      DestY = (SizeOfY - Height) / 2;
      break;

    default:
      DestX = CoordinateX;
      DestY = CoordinateY;
      break;
    }

    if ((DestX >= 0) && (DestY >= 0)) {
      if (GraphicsOutput != NULL) {
        Status = GraphicsOutput->Blt (
                            GraphicsOutput,
                            Blt,
                            EfiBltBufferToVideo,
                            0,
                            0,
                            (UINTN) DestX,
                            (UINTN) DestY,
                            Width,
                            Height,
                            Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                            );
      } else if (UgaDraw != NULL && FeaturePcdGet (PcdUgaConsumeSupport)) {
        Status = UgaDraw->Blt (
                            UgaDraw,
                            (EFI_UGA_PIXEL *) Blt,
                            EfiUgaBltBufferToVideo,
                            0,
                            0,
                            (UINTN) DestX,
                            (UINTN) DestY,
                            Width,
                            Height,
                            Width * sizeof (EFI_UGA_PIXEL)
                            );
      } else {
        Status = EFI_UNSUPPORTED;
      }

      //
      // Report displayed Logo information.
      //
      if (!EFI_ERROR (Status)) {
        NumberOfLogos++;

        if (LogoWidth == 0) {
          //
          // The first Logo.
          //
          LogoDestX = (UINTN) DestX;
          LogoDestY = (UINTN) DestY;
          LogoWidth = Width;
          LogoHeight = Height;
        } else {
          //
          // Merge new logo with old one.
          //
          NewDestX = MIN ((UINTN) DestX, LogoDestX);
          NewDestY = MIN ((UINTN) DestY, LogoDestY);
          NewWidth = MAX ((UINTN) DestX + Width, LogoDestX + LogoWidth) - NewDestX;
          NewHeight = MAX ((UINTN) DestY + Height, LogoDestY + LogoHeight) - NewDestY;

          LogoDestX = NewDestX;
          LogoDestY = NewDestY;
          LogoWidth = NewWidth;
          LogoHeight = NewHeight;
        }
      }
    }

    FreePool (ImageData);

    if (Badging == NULL) {
      break;
    }
  }

Done:
  if (BootLogo == NULL || NumberOfLogos == 0) {
    //
    // No logo displayed.
    //
    if (Blt != NULL) {
      FreePool (Blt);
    }

    return Status;
  }

  //
  // Advertise displayed Logo information.
  //
  if (NumberOfLogos == 1) {
    //
    // Only one logo displayed, use its Blt buffer directly for BootLogo protocol.
    //
    LogoBlt = Blt;
    Status = EFI_SUCCESS;
  } else {
    //
    // More than one Logo displayed, get merged BltBuffer using VideoToBuffer operation. 
    //
    if (Blt != NULL) {
      FreePool (Blt);
    }

    LogoBlt = AllocateZeroPool (LogoWidth * LogoHeight * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    if (LogoBlt == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    if (GraphicsOutput != NULL) {
      Status = GraphicsOutput->Blt (
                          GraphicsOutput,
                          LogoBlt,
                          EfiBltVideoToBltBuffer,
                          LogoDestX,
                          LogoDestY,
                          0,
                          0,
                          LogoWidth,
                          LogoHeight,
                          LogoWidth * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                          );
    } else if (UgaDraw != NULL && FeaturePcdGet (PcdUgaConsumeSupport)) {
      Status = UgaDraw->Blt (
                          UgaDraw,
                          (EFI_UGA_PIXEL *) LogoBlt,
                          EfiUgaVideoToBltBuffer,
                          LogoDestX,
                          LogoDestY,
                          0,
                          0,
                          LogoWidth,
                          LogoHeight,
                          LogoWidth * sizeof (EFI_UGA_PIXEL)
                          );
    } else {
      Status = EFI_UNSUPPORTED;
    }
  }

  if (!EFI_ERROR (Status)) {
    BootLogo->SetBootLogo (BootLogo, LogoBlt, LogoDestX, LogoDestY, LogoWidth, LogoHeight);
  }
  FreePool (LogoBlt);

  return Status;
}

/**
  Use SystemTable Conout to turn on video based Simple Text Out consoles. The 
  Simple Text Out screens will now be synced up with all non video output devices

  @retval EFI_SUCCESS     UGA devices are back in text mode and synced up.

**/
EFI_STATUS
EFIAPI
DisableQuietBoot (
  VOID
  )
{

  //
  // Enable Cursor on Screen
  //
  gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  return EFI_SUCCESS;
}


/**

  Show progress bar with title above it. It only works in Graphics mode.


  @param TitleForeground Foreground color for Title.
  @param TitleBackground Background color for Title.
  @param Title           Title above progress bar.
  @param ProgressColor   Progress bar color.
  @param Progress        Progress (0-100)
  @param PreviousValue   The previous value of the progress.

  @retval  EFI_STATUS       Success update the progress bar

**/
EFI_STATUS
EFIAPI
ShowProgress (
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL TitleForeground,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL TitleBackground,
  IN CHAR16                        *Title,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL ProgressColor,
  IN UINTN                         Progress,
  IN UINTN                         PreviousValue
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *GraphicsOutput;
  EFI_UGA_DRAW_PROTOCOL          *UgaDraw;
  UINT32                         SizeOfX;
  UINT32                         SizeOfY;
  UINT32                         ColorDepth;
  UINT32                         RefreshRate;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color;
  UINTN                          BlockHeight;
  UINTN                          BlockWidth;
  UINTN                          BlockNum;
  UINTN                          PosX;
  UINTN                          PosY;
  UINTN                          Index;

  if (Progress > 100) {
    return EFI_INVALID_PARAMETER;
  }

  UgaDraw = NULL;
  Status = gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **) &GraphicsOutput
                  );
  if (EFI_ERROR (Status) && FeaturePcdGet (PcdUgaConsumeSupport)) {
    GraphicsOutput = NULL;

    Status = gBS->HandleProtocol (
                    gST->ConsoleOutHandle,
                    &gEfiUgaDrawProtocolGuid,
                    (VOID **) &UgaDraw
                    );
  }
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  SizeOfX = 0;
  SizeOfY = 0;
  if (GraphicsOutput != NULL) {
    SizeOfX = GraphicsOutput->Mode->Info->HorizontalResolution;
    SizeOfY = GraphicsOutput->Mode->Info->VerticalResolution;
  } else if (UgaDraw != NULL) {
    Status = UgaDraw->GetMode (
                        UgaDraw,
                        &SizeOfX,
                        &SizeOfY,
                        &ColorDepth,
                        &RefreshRate
                        );
    if (EFI_ERROR (Status)) {
      return EFI_UNSUPPORTED;
    }
  } else {
    return EFI_UNSUPPORTED;
  }

  BlockWidth  = SizeOfX / 100;
  BlockHeight = SizeOfY / 50;

  BlockNum    = Progress;

  PosX        = 0;
  PosY        = SizeOfY * 48 / 50;

  if (BlockNum == 0) {
    //
    // Clear progress area
    //
    SetMem (&Color, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL), 0x0);

    if (GraphicsOutput != NULL) {
      Status = GraphicsOutput->Blt (
                          GraphicsOutput,
                          &Color,
                          EfiBltVideoFill,
                          0,
                          0,
                          0,
                          PosY - EFI_GLYPH_HEIGHT - 1,
                          SizeOfX,
                          SizeOfY - (PosY - EFI_GLYPH_HEIGHT - 1),
                          SizeOfX * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                          );
    } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
      Status = UgaDraw->Blt (
                          UgaDraw,
                          (EFI_UGA_PIXEL *) &Color,
                          EfiUgaVideoFill,
                          0,
                          0,
                          0,
                          PosY - EFI_GLYPH_HEIGHT - 1,
                          SizeOfX,
                          SizeOfY - (PosY - EFI_GLYPH_HEIGHT - 1),
                          SizeOfX * sizeof (EFI_UGA_PIXEL)
                          );
    } else {
      return EFI_UNSUPPORTED;
    }
  }
  //
  // Show progress by drawing blocks
  //
  for (Index = PreviousValue; Index < BlockNum; Index++) {
    PosX = Index * BlockWidth;
    if (GraphicsOutput != NULL) {
      Status = GraphicsOutput->Blt (
                          GraphicsOutput,
                          &ProgressColor,
                          EfiBltVideoFill,
                          0,
                          0,
                          PosX,
                          PosY,
                          BlockWidth - 1,
                          BlockHeight,
                          (BlockWidth) * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                          );
    } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
      Status = UgaDraw->Blt (
                          UgaDraw,
                          (EFI_UGA_PIXEL *) &ProgressColor,
                          EfiUgaVideoFill,
                          0,
                          0,
                          PosX,
                          PosY,
                          BlockWidth - 1,
                          BlockHeight,
                          (BlockWidth) * sizeof (EFI_UGA_PIXEL)
                          );
    } else {
      return EFI_UNSUPPORTED;
    }
  }

  PrintXY (
    (SizeOfX - StrLen (Title) * EFI_GLYPH_WIDTH) / 2,
    PosY - EFI_GLYPH_HEIGHT - 1,
    &TitleForeground,
    &TitleBackground,
    Title
    );

  return EFI_SUCCESS;
}