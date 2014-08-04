/* vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * Copyright 2014 Hewlett-Packard Development Company, L.P.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain 
 * a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 */

#include "command.h"
#include "net.h"

ascore_command_status_t ascore_command_send(ascon_st *con, ascore_command_t command, char *data, size_t length)
{
  uv_buf_t send_buffer[3];
  int ret;

  if (con->status != ASCORE_CON_STATUS_IDLE)
  {
    /* Server not ready */
    return con->command_status;
  }

  asdebug("Sending command %02X to sever", command);
  con->local_errcode= ASRET_OK;
  con->errmsg[0]= '\0';

  ascore_pack_int3(con->packet_header, length + 1);
  con->packet_number= 0;
  con->packet_header[3] = con->packet_number;

  con->write_buffer[0]= command;
  send_buffer[0].base= con->packet_header;
  send_buffer[0].len= 4;
  send_buffer[1].base= con->write_buffer;
  send_buffer[1].len= 1;
  if (length > 0)
  {
    send_buffer[2].base= data;
    send_buffer[2].len= length;
    asdebug("Sending %lld bytes with command to server", length);
    asdebug_hex(data, length);
    ret= uv_write(&con->uv_objects.write_req, con->uv_objects.stream, send_buffer, 3, on_write);
  }
  else
  {
    asdebug("Sending command with no data");
    ret= uv_write(&con->uv_objects.write_req, con->uv_objects.stream, send_buffer, 2, on_write);
  }
  if (ret != 0)
  {
    con->local_errcode= ASRET_NET_WRITE_ERROR;
    asdebug("Write fail: %s", uv_err_name(uv_last_error(con->uv_objects.loop)));
    con->command_status= ASCORE_COMMAND_STATUS_SEND_FAILED;
    con->next_packet_type= ASCORE_PACKET_TYPE_NONE;
    con->local_errcode= ASRET_NET_WRITE_ERROR;
    snprintf(con->errmsg, ASCORE_ERROR_BUFFER_SIZE, "Query send failed: %s", uv_err_name(uv_last_error(con->uv_objects.loop)));
    return con->command_status;
  }

  con->next_packet_type= ASCORE_PACKET_TYPE_RESPONSE;
  uv_read_start(con->uv_objects.stream, on_alloc, ascore_read_data_cb);
  con->command_status= ASCORE_COMMAND_STATUS_SEND;
  con->status= ASCORE_CON_STATUS_BUSY;
  if (con->options.polling)
  {
    uv_run(con->uv_objects.loop, UV_RUN_NOWAIT);
  }
  else
  {
    uv_run(con->uv_objects.loop, UV_RUN_DEFAULT);
  }
  return ASCORE_COMMAND_STATUS_SEND;
}

ascore_command_status_t ascore_get_next_row(ascon_st *con)
{
  ascore_buffer_packet_read_end(con->read_buffer);
  con->next_packet_type= ASCORE_PACKET_TYPE_ROW;
  con->command_status= ASCORE_COMMAND_STATUS_READ_ROW;
  ascore_con_process_packets(con);
  if (con->options.polling)
  {
    ascore_con_process_packets(con);
  }
  else
  {
    while (not ascore_con_process_packets(con))
    {
      uv_read_start(con->uv_objects.stream, on_alloc, ascore_read_data_cb);
      uv_run(con->uv_objects.loop, UV_RUN_DEFAULT);
    }
  }
  return con->command_status;
}